#!/usr/bin/env python3
"""用于本项目 PLC 握手联调的零依赖 Modbus TCP 图形模拟器。"""

from __future__ import annotations

import argparse
import queue
import socket
import struct
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from typing import Callable


MAX_ADDRESS = 65535


def _timestamp() -> str:
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


class ModbusDataModel:
    """线程安全的线圈和保持寄存器数据区。"""

    def __init__(self) -> None:
        self._coils = bytearray(MAX_ADDRESS + 1)
        self._registers = [0] * (MAX_ADDRESS + 1)
        self._lock = threading.RLock()
        self._coil_listeners: list[Callable[[int, bool, str], None]] = []

    def add_coil_listener(self, listener: Callable[[int, bool, str], None]) -> None:
        with self._lock:
            self._coil_listeners.append(listener)

    def read_coils(self, address: int, count: int) -> list[bool]:
        self._validate_range(address, count)
        with self._lock:
            return [bool(value) for value in self._coils[address : address + count]]

    def get_coil(self, address: int) -> bool:
        return self.read_coils(address, 1)[0]

    def set_coil(self, address: int, value: bool, source: str) -> None:
        self._validate_range(address, 1)
        listeners: list[Callable[[int, bool, str], None]] = []
        changed = False
        with self._lock:
            encoded = 1 if value else 0
            if self._coils[address] != encoded:
                self._coils[address] = encoded
                changed = True
                listeners = list(self._coil_listeners)
        if changed:
            for listener in listeners:
                listener(address, value, source)

    def read_registers(self, address: int, count: int) -> list[int]:
        self._validate_range(address, count)
        with self._lock:
            return list(self._registers[address : address + count])

    def set_register(self, address: int, value: int) -> None:
        self._validate_range(address, 1)
        if not 0 <= value <= 0xFFFF:
            raise ValueError("register value must be in [0, 65535]")
        with self._lock:
            self._registers[address] = value

    @staticmethod
    def _validate_range(address: int, count: int) -> None:
        if address < 0 or count <= 0 or address + count > MAX_ADDRESS + 1:
            raise ValueError("address range is outside [0, 65535]")


@dataclass(frozen=True)
class ServerEndpoint:
    host: str
    port: int
    unit_id: int


class ModbusTcpServer:
    """支持本项目所用 FC01/03/05/06 的小型 Modbus TCP Server。"""

    def __init__(
        self,
        model: ModbusDataModel,
        log: Callable[[str], None] | None = None,
        client_count_changed: Callable[[int], None] | None = None,
    ) -> None:
        self.model = model
        self.log = log or (lambda _message: None)
        self.client_count_changed = client_count_changed or (lambda _count: None)
        self.trace_reads = False
        self._unit_id = 1
        self._listener: socket.socket | None = None
        self._accept_thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._clients: set[socket.socket] = set()
        self._clients_lock = threading.Lock()
        self._bound_endpoint: ServerEndpoint | None = None

    @property
    def running(self) -> bool:
        thread = self._accept_thread
        return thread is not None and thread.is_alive() and not self._stop_event.is_set()

    @property
    def endpoint(self) -> ServerEndpoint | None:
        return self._bound_endpoint

    def start(self, host: str, port: int, unit_id: int) -> ServerEndpoint:
        if self.running:
            raise RuntimeError("Modbus TCP Server 已经启动")
        if not host:
            raise ValueError("监听地址不能为空")
        if not 0 <= port <= 65535:
            raise ValueError("端口必须在 0-65535 之间")
        if not 0 <= unit_id <= 255:
            raise ValueError("Unit ID 必须在 0-255 之间")

        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            listener.bind((host, port))
            listener.listen(8)
            listener.settimeout(0.25)
        except Exception:
            listener.close()
            raise

        actual_host, actual_port = listener.getsockname()[:2]
        self._listener = listener
        self._unit_id = unit_id
        self._stop_event.clear()
        self._bound_endpoint = ServerEndpoint(str(actual_host), int(actual_port), unit_id)
        self._accept_thread = threading.Thread(
            target=self._accept_loop,
            name="plc-simulator-accept",
            daemon=True,
        )
        self._accept_thread.start()
        self.log(f"Server 已启动：{actual_host}:{actual_port}，Unit ID={unit_id}")
        return self._bound_endpoint

    def stop(self) -> None:
        self._stop_event.set()
        listener = self._listener
        self._listener = None
        if listener is not None:
            try:
                listener.close()
            except OSError:
                pass

        with self._clients_lock:
            clients = list(self._clients)
        for client in clients:
            try:
                client.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                client.close()
            except OSError:
                pass

        thread = self._accept_thread
        self._accept_thread = None
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=1.5)
        self._bound_endpoint = None
        self.log("Server 已停止")

    def _accept_loop(self) -> None:
        while not self._stop_event.is_set():
            listener = self._listener
            if listener is None:
                break
            try:
                client, address = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            client.settimeout(0.5)
            with self._clients_lock:
                self._clients.add(client)
                count = len(self._clients)
            self.client_count_changed(count)
            self.log(f"客户端已连接：{address[0]}:{address[1]}")
            threading.Thread(
                target=self._client_loop,
                args=(client, address),
                name=f"plc-simulator-client-{address[0]}:{address[1]}",
                daemon=True,
            ).start()

    def _client_loop(self, client: socket.socket, address: tuple[str, int]) -> None:
        try:
            while not self._stop_event.is_set():
                header = self._receive_exact(client, 7)
                if header is None:
                    break
                transaction_id, protocol_id, length, request_unit = struct.unpack(
                    ">HHHB", header
                )
                if protocol_id != 0 or length < 2 or length > 254:
                    self.log(
                        f"拒绝非法 MBAP：Protocol={protocol_id}，Length={length}"
                    )
                    break
                pdu = self._receive_exact(client, length - 1)
                if pdu is None:
                    break
                response_pdu = self._process_pdu(request_unit, pdu)
                response = struct.pack(
                    ">HHHB",
                    transaction_id,
                    0,
                    len(response_pdu) + 1,
                    request_unit,
                ) + response_pdu
                client.sendall(response)
        except (ConnectionError, OSError, struct.error) as error:
            if not self._stop_event.is_set():
                self.log(f"客户端通讯结束：{error}")
        finally:
            try:
                client.close()
            except OSError:
                pass
            with self._clients_lock:
                self._clients.discard(client)
                count = len(self._clients)
            self.client_count_changed(count)
            self.log(f"客户端已断开：{address[0]}:{address[1]}")

    def _receive_exact(self, client: socket.socket, size: int) -> bytes | None:
        data = bytearray()
        while len(data) < size and not self._stop_event.is_set():
            try:
                chunk = client.recv(size - len(data))
            except socket.timeout:
                continue
            if not chunk:
                return None
            data.extend(chunk)
        return bytes(data) if len(data) == size else None

    def _process_pdu(self, request_unit: int, pdu: bytes) -> bytes:
        if not pdu:
            return bytes((0x80, 3))
        function = pdu[0]
        if request_unit != self._unit_id:
            self.log(
                f"Unit ID 不匹配：收到 {request_unit}，模拟器配置为 {self._unit_id}"
            )
            return bytes((function | 0x80, 11))
        try:
            if function == 1:
                return self._read_coils(pdu)
            if function == 3:
                return self._read_holding_registers(pdu)
            if function == 5:
                return self._write_single_coil(pdu)
            if function == 6:
                return self._write_single_register(pdu)
            return bytes((function | 0x80, 1))
        except ValueError:
            return bytes((function | 0x80, 2))
        except struct.error:
            return bytes((function | 0x80, 3))

    def _read_coils(self, pdu: bytes) -> bytes:
        if len(pdu) != 5:
            return bytes((0x81, 3))
        address, count = struct.unpack(">HH", pdu[1:])
        if not 1 <= count <= 2000:
            return bytes((0x81, 3))
        values = self.model.read_coils(address, count)
        packed = bytearray((count + 7) // 8)
        for index, value in enumerate(values):
            if value:
                packed[index // 8] |= 1 << (index % 8)
        if self.trace_reads:
            self.log(f"FC01 读取线圈：地址={address}，数量={count}")
        return bytes((1, len(packed))) + bytes(packed)

    def _read_holding_registers(self, pdu: bytes) -> bytes:
        if len(pdu) != 5:
            return bytes((0x83, 3))
        address, count = struct.unpack(">HH", pdu[1:])
        if not 1 <= count <= 125:
            return bytes((0x83, 3))
        values = self.model.read_registers(address, count)
        payload = b"".join(struct.pack(">H", value) for value in values)
        if self.trace_reads:
            self.log(f"FC03 读取保持寄存器：地址={address}，数量={count}")
        return bytes((3, len(payload))) + payload

    def _write_single_coil(self, pdu: bytes) -> bytes:
        if len(pdu) != 5:
            return bytes((0x85, 3))
        address, encoded = struct.unpack(">HH", pdu[1:])
        if encoded not in (0x0000, 0xFF00):
            return bytes((0x85, 3))
        self.model.set_coil(address, encoded == 0xFF00, "视觉客户端 FC05")
        return pdu

    def _write_single_register(self, pdu: bytes) -> bytes:
        if len(pdu) != 5:
            return bytes((0x86, 3))
        address, value = struct.unpack(">HH", pdu[1:])
        self.model.set_register(address, value)
        self.log(f"FC06 写保持寄存器：地址={address}，值={value}")
        return pdu


class PlcSimulatorApp:
    SIGNALS = (
        ("Busy", "视觉 → PLC", 1),
        ("Done", "视觉 → PLC", 2),
        ("OK", "视觉 → PLC", 3),
        ("NG", "视觉 → PLC", 4),
        ("Error", "视觉 → PLC", 5),
        ("Heartbeat", "视觉 → PLC", 6),
        ("ACK", "PLC → 视觉", 7),
    )

    def __init__(self) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.tk = tk
        self.ttk = ttk
        self.root = tk.Tk()
        self.root.title("IMgui OpenCV · Modbus TCP PLC 模拟器")
        self.root.geometry("1080x820")
        self.root.minsize(900, 650)
        self.root.option_add("*Font", ("Microsoft YaHei UI", 10))

        self.model = ModbusDataModel()
        self.ui_events: queue.Queue[tuple] = queue.Queue()
        self.model.add_coil_listener(self._on_coil_changed)
        self.server = ModbusTcpServer(
            self.model,
            log=lambda message: self.ui_events.put(("log", message)),
            client_count_changed=lambda count: self.ui_events.put(
                ("clients", count)
            ),
        )

        self.host_var = tk.StringVar(value="127.0.0.1")
        self.port_var = tk.StringVar(value="1502")
        self.unit_id_var = tk.StringVar(value="1")
        self.server_status_var = tk.StringVar(value="未启动")
        self.client_count_var = tk.StringVar(value="客户端 0")
        self.trace_reads_var = tk.BooleanVar(value=False)
        self.auto_ack_var = tk.BooleanVar(value=True)
        self.auto_ack_delay_var = tk.StringVar(value="300")
        self.trigger_pulse_var = tk.StringVar(value="500")
        self.ack_pulse_var = tk.StringVar(value="250")
        self.signal_address_vars: dict[str, tk.StringVar] = {}
        self.signal_state_labels: dict[str, ttk.Label] = {}
        self.task_rows: list[tuple[tk.StringVar, tk.StringVar, ttk.Label]] = []
        self._pulse_generations: dict[int, int] = {}
        self._auto_ack_pending = False

        self._build_ui()
        self._update_all_indicators()
        self.root.protocol("WM_DELETE_WINDOW", self._close)
        self.root.after(50, self._drain_events)

    def _build_ui(self) -> None:
        tk = self.tk
        ttk = self.ttk
        root = self.root

        outer = ttk.Frame(root, padding=14)
        outer.pack(fill="both", expand=True)

        title = ttk.Label(
            outer,
            text="Modbus TCP PLC 握手模拟器",
            font=("Microsoft YaHei UI", 17, "bold"),
        )
        title.pack(anchor="w")
        ttk.Label(
            outer,
            text="模拟 PLC Trigger/ACK 输入，并观察视觉程序写回的 Busy、Done、OK、NG、Error 和 Heartbeat。",
            foreground="#5f6b7a",
        ).pack(anchor="w", pady=(2, 12))

        connection = ttk.LabelFrame(outer, text="1. Modbus TCP Server", padding=10)
        connection.pack(fill="x")
        ttk.Label(connection, text="监听地址").grid(row=0, column=0, sticky="w")
        ttk.Entry(connection, textvariable=self.host_var, width=16).grid(
            row=0, column=1, padx=(6, 14)
        )
        ttk.Label(connection, text="端口").grid(row=0, column=2, sticky="w")
        ttk.Entry(connection, textvariable=self.port_var, width=8).grid(
            row=0, column=3, padx=(6, 14)
        )
        ttk.Label(connection, text="Unit ID").grid(row=0, column=4, sticky="w")
        ttk.Entry(connection, textvariable=self.unit_id_var, width=6).grid(
            row=0, column=5, padx=(6, 14)
        )
        self.start_button = ttk.Button(
            connection, text="启动 Server", command=self._toggle_server
        )
        self.start_button.grid(row=0, column=6, padx=(4, 12))
        ttk.Label(connection, textvariable=self.server_status_var).grid(
            row=0, column=7, padx=(0, 12)
        )
        ttk.Label(connection, textvariable=self.client_count_var).grid(row=0, column=8)
        ttk.Checkbutton(
            connection,
            text="记录 FC01/FC03 轮询",
            variable=self.trace_reads_var,
            command=self._update_trace_reads,
        ).grid(row=1, column=0, columnspan=3, sticky="w", pady=(9, 0))
        ttk.Label(
            connection,
            text="主程序连接参数填写 127.0.0.1、1502、Unit ID 1。",
            foreground="#5f6b7a",
        ).grid(row=1, column=3, columnspan=6, sticky="w", pady=(9, 0))

        middle = ttk.Frame(outer)
        middle.pack(fill="both", expand=True, pady=(12, 0))
        middle.columnconfigure(0, weight=1)
        middle.columnconfigure(1, weight=1)
        middle.rowconfigure(0, weight=1)

        handshake = ttk.LabelFrame(middle, text="2. 握手信号", padding=10)
        handshake.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        self._build_signal_table(handshake)

        tasks = ttk.LabelFrame(middle, text="3. 独立任务 Trigger", padding=10)
        tasks.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        self._build_task_table(tasks)

        log_frame = ttk.LabelFrame(outer, text="通讯与状态日志", padding=8)
        log_frame.pack(fill="both", expand=False, pady=(12, 0))
        self.log_text = tk.Text(
            log_frame,
            height=10,
            wrap="word",
            state="disabled",
            background="#101820",
            foreground="#d9e2ec",
            insertbackground="#ffffff",
        )
        log_scroll = ttk.Scrollbar(
            log_frame, orient="vertical", command=self.log_text.yview
        )
        self.log_text.configure(yscrollcommand=log_scroll.set)
        self.log_text.pack(side="left", fill="both", expand=True)
        log_scroll.pack(side="right", fill="y")

    def _build_signal_table(self, parent) -> None:
        ttk = self.ttk
        tk = self.tk
        headers = ("信号", "方向", "地址", "当前值")
        for column, text in enumerate(headers):
            ttk.Label(parent, text=text, font=("Microsoft YaHei UI", 10, "bold")).grid(
                row=0, column=column, sticky="w", padx=(0, 10), pady=(0, 7)
            )
        for row, (name, direction, address) in enumerate(self.SIGNALS, start=1):
            ttk.Label(parent, text=name).grid(row=row, column=0, sticky="w", pady=3)
            ttk.Label(parent, text=direction).grid(
                row=row, column=1, sticky="w", padx=(0, 10), pady=3
            )
            address_var = tk.StringVar(value=str(address))
            self.signal_address_vars[name] = address_var
            ttk.Entry(parent, textvariable=address_var, width=7).grid(
                row=row, column=2, sticky="w", padx=(0, 10), pady=3
            )
            state_label = ttk.Label(parent, text="OFF", foreground="#6b7280")
            state_label.grid(row=row, column=3, sticky="w", pady=3)
            self.signal_state_labels[name] = state_label

        controls_row = len(self.SIGNALS) + 1
        ttk.Checkbutton(
            parent, text="Done 后自动 ACK", variable=self.auto_ack_var
        ).grid(row=controls_row, column=0, columnspan=2, sticky="w", pady=(12, 4))
        ttk.Label(parent, text="延迟 ms").grid(
            row=controls_row, column=2, sticky="e", pady=(12, 4)
        )
        ttk.Entry(parent, textvariable=self.auto_ack_delay_var, width=7).grid(
            row=controls_row, column=3, sticky="w", pady=(12, 4)
        )
        ttk.Button(parent, text="手动发送 ACK", command=self._pulse_ack).grid(
            row=controls_row + 1, column=0, columnspan=2, sticky="ew", pady=4
        )
        ttk.Button(parent, text="全部线圈复位", command=self._reset_mapped_coils).grid(
            row=controls_row + 1, column=2, columnspan=2, sticky="ew", pady=4
        )
        ttk.Label(parent, text="ACK 脉冲 ms").grid(
            row=controls_row + 2, column=0, sticky="w", pady=(7, 0)
        )
        ttk.Entry(parent, textvariable=self.ack_pulse_var, width=8).grid(
            row=controls_row + 2, column=1, sticky="w", pady=(7, 0)
        )
        ttk.Label(
            parent,
            text="地址均为 Modbus 零基协议地址。",
            foreground="#5f6b7a",
        ).grid(
            row=controls_row + 3,
            column=0,
            columnspan=4,
            sticky="w",
            pady=(12, 0),
        )

    def _build_task_table(self, parent) -> None:
        ttk = self.ttk
        tk = self.tk
        parent.rowconfigure(1, weight=1)
        parent.columnconfigure(0, weight=1)

        settings = ttk.Frame(parent)
        settings.grid(row=0, column=0, sticky="ew", pady=(0, 7))
        ttk.Label(settings, text="Trigger 脉冲 ms").pack(side="left")
        ttk.Entry(settings, textvariable=self.trigger_pulse_var, width=8).pack(
            side="left", padx=(6, 12)
        )
        ttk.Label(
            settings,
            text="任务01=0，任务02起从8递增",
            foreground="#5f6b7a",
        ).pack(side="left")

        canvas = tk.Canvas(parent, highlightthickness=0)
        scrollbar = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
        rows = ttk.Frame(canvas)
        rows.bind(
            "<Configure>",
            lambda _event: canvas.configure(scrollregion=canvas.bbox("all")),
        )
        canvas.create_window((0, 0), window=rows, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.grid(row=1, column=0, sticky="nsew")
        scrollbar.grid(row=1, column=1, sticky="ns")

        headers = ("任务", "Trigger 地址", "当前值", "操作")
        for column, text in enumerate(headers):
            ttk.Label(rows, text=text, font=("Microsoft YaHei UI", 10, "bold")).grid(
                row=0, column=column, sticky="w", padx=(0, 9), pady=(0, 6)
            )

        for index in range(16):
            default_address = 0 if index == 0 else index + 7
            name_var = tk.StringVar(value=f"任务{index + 1:02d}")
            address_var = tk.StringVar(value=str(default_address))
            ttk.Entry(rows, textvariable=name_var, width=13).grid(
                row=index + 1, column=0, sticky="ew", padx=(0, 7), pady=3
            )
            ttk.Entry(rows, textvariable=address_var, width=9).grid(
                row=index + 1, column=1, sticky="w", padx=(0, 7), pady=3
            )
            state_label = ttk.Label(rows, text="OFF", foreground="#6b7280")
            state_label.grid(row=index + 1, column=2, sticky="w", padx=(0, 7))
            ttk.Button(
                rows,
                text="触发拍照",
                command=lambda row_index=index: self._trigger_task(row_index),
            ).grid(row=index + 1, column=3, sticky="ew", pady=3)
            self.task_rows.append((name_var, address_var, state_label))

    def _toggle_server(self) -> None:
        from tkinter import messagebox

        if self.server.running:
            self.server.stop()
            self.start_button.configure(text="启动 Server")
            self.server_status_var.set("未启动")
            return
        try:
            endpoint = self.server.start(
                self.host_var.get().strip(),
                self._parse_int(self.port_var.get(), "端口", 0, 65535),
                self._parse_int(self.unit_id_var.get(), "Unit ID", 0, 255),
            )
        except Exception as error:
            messagebox.showerror("启动失败", str(error), parent=self.root)
            return
        self.port_var.set(str(endpoint.port))
        self.start_button.configure(text="停止 Server")
        self.server_status_var.set("在线")

    def _trigger_task(self, row_index: int) -> None:
        from tkinter import messagebox

        name_var, address_var, _label = self.task_rows[row_index]
        try:
            address = self._parse_int(
                address_var.get(), f"{name_var.get()} Trigger 地址", 0, MAX_ADDRESS
            )
            duration = self._parse_int(
                self.trigger_pulse_var.get(), "Trigger 脉冲", 50, 60000
            )
        except ValueError as error:
            messagebox.showerror("参数错误", str(error), parent=self.root)
            return
        self._pulse_coil(address, duration, f"模拟 PLC · {name_var.get()} Trigger")

    def _pulse_ack(self) -> None:
        from tkinter import messagebox

        try:
            address = self._signal_address("ACK")
            duration = self._parse_int(self.ack_pulse_var.get(), "ACK 脉冲", 50, 60000)
        except ValueError as error:
            messagebox.showerror("参数错误", str(error), parent=self.root)
            return
        self._pulse_coil(address, duration, "模拟 PLC · ACK")

    def _pulse_coil(self, address: int, duration_ms: int, source: str) -> None:
        generation = self._pulse_generations.get(address, 0) + 1
        self._pulse_generations[address] = generation

        def start_pulse() -> None:
            if self._pulse_generations.get(address) != generation:
                return
            self.model.set_coil(address, True, source)
            self.root.after(duration_ms, finish_pulse)

        def finish_pulse() -> None:
            if self._pulse_generations.get(address) != generation:
                return
            self.model.set_coil(address, False, source)

        if self.model.get_coil(address):
            self.model.set_coil(address, False, source)
            self.root.after(75, start_pulse)
        else:
            start_pulse()

    def _reset_mapped_coils(self) -> None:
        addresses: set[int] = set()
        for name in self.signal_address_vars:
            try:
                addresses.add(self._signal_address(name))
            except ValueError:
                pass
        for _name_var, address_var, _label in self.task_rows:
            try:
                addresses.add(self._parse_int(address_var.get(), "地址", 0, MAX_ADDRESS))
            except ValueError:
                pass
        for address in addresses:
            self._pulse_generations[address] = self._pulse_generations.get(address, 0) + 1
            self.model.set_coil(address, False, "模拟 PLC · 全部复位")
        self._auto_ack_pending = False

    def _on_coil_changed(self, address: int, value: bool, source: str) -> None:
        self.ui_events.put(("coil", address, value, source))

    def _drain_events(self) -> None:
        try:
            while True:
                event = self.ui_events.get_nowait()
                if event[0] == "log":
                    self._append_log(event[1])
                elif event[0] == "clients":
                    self.client_count_var.set(f"客户端 {event[1]}")
                elif event[0] == "coil":
                    _kind, address, value, source = event
                    self._append_log(
                        f"线圈 {address} ← {'ON' if value else 'OFF'}（{source}）"
                    )
                    self._update_all_indicators()
                    self._maybe_schedule_auto_ack(address, value, source)
        except queue.Empty:
            pass
        self.root.after(50, self._drain_events)

    def _maybe_schedule_auto_ack(
        self, address: int, value: bool, source: str
    ) -> None:
        if (
            not value
            or not self.auto_ack_var.get()
            or self._auto_ack_pending
            or source != "视觉客户端 FC05"
        ):
            return
        try:
            done_address = self._signal_address("Done")
            delay = self._parse_int(
                self.auto_ack_delay_var.get(), "自动 ACK 延迟", 0, 60000
            )
            pulse = self._parse_int(self.ack_pulse_var.get(), "ACK 脉冲", 50, 60000)
        except ValueError as error:
            self._append_log(f"自动 ACK 参数错误：{error}")
            return
        if address != done_address:
            return
        self._auto_ack_pending = True

        def send_ack() -> None:
            try:
                self._pulse_ack()
            finally:
                self.root.after(pulse + 100, self._clear_auto_ack_pending)

        self._append_log(f"检测到 Done，{delay} ms 后自动发送 ACK")
        self.root.after(delay, send_ack)

    def _clear_auto_ack_pending(self) -> None:
        self._auto_ack_pending = False

    def _update_all_indicators(self) -> None:
        for name, label in self.signal_state_labels.items():
            try:
                active = self.model.get_coil(self._signal_address(name))
            except ValueError:
                label.configure(text="地址错误", foreground="#dc2626")
                continue
            label.configure(
                text="ON" if active else "OFF",
                foreground="#16a34a" if active else "#6b7280",
            )
        for _name_var, address_var, label in self.task_rows:
            try:
                address = self._parse_int(address_var.get(), "地址", 0, MAX_ADDRESS)
                active = self.model.get_coil(address)
            except ValueError:
                label.configure(text="地址错误", foreground="#dc2626")
                continue
            label.configure(
                text="ON" if active else "OFF",
                foreground="#ea580c" if active else "#6b7280",
            )

    def _signal_address(self, name: str) -> int:
        return self._parse_int(
            self.signal_address_vars[name].get(), f"{name} 地址", 0, MAX_ADDRESS
        )

    @staticmethod
    def _parse_int(text: str, name: str, minimum: int, maximum: int) -> int:
        try:
            value = int(text.strip())
        except ValueError as error:
            raise ValueError(f"{name} 必须是整数") from error
        if not minimum <= value <= maximum:
            raise ValueError(f"{name} 必须在 {minimum}-{maximum} 之间")
        return value

    def _update_trace_reads(self) -> None:
        self.server.trace_reads = self.trace_reads_var.get()

    def _append_log(self, message: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", f"[{_timestamp()}] {message}\n")
        line_count = int(self.log_text.index("end-1c").split(".")[0])
        if line_count > 800:
            self.log_text.delete("1.0", "201.0")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _close(self) -> None:
        self.server.stop()
        self.root.destroy()

    def run(self) -> None:
        self._append_log("请先启动 Server，再让视觉主程序连接本机 1502 端口。")
        self.root.mainloop()


def _exchange(sock: socket.socket, transaction: int, unit: int, pdu: bytes) -> bytes:
    request = struct.pack(">HHHB", transaction, 0, len(pdu) + 1, unit) + pdu
    sock.sendall(request)
    header = b""
    while len(header) < 7:
        chunk = sock.recv(7 - len(header))
        if not chunk:
            raise RuntimeError("self-test connection closed")
        header += chunk
    response_transaction, protocol, length, response_unit = struct.unpack(
        ">HHHB", header
    )
    if (response_transaction, protocol, response_unit) != (transaction, 0, unit):
        raise AssertionError("self-test MBAP mismatch")
    body = b""
    while len(body) < length - 1:
        chunk = sock.recv(length - 1 - len(body))
        if not chunk:
            raise RuntimeError("self-test response truncated")
        body += chunk
    return body


def run_self_test() -> None:
    model = ModbusDataModel()
    messages: list[str] = []
    server = ModbusTcpServer(model, log=messages.append)
    endpoint = server.start("127.0.0.1", 0, 1)
    try:
        with socket.create_connection((endpoint.host, endpoint.port), timeout=2.0) as sock:
            sock.settimeout(2.0)
            assert _exchange(sock, 1, 1, bytes((5, 0, 12, 0xFF, 0))) == bytes(
                (5, 0, 12, 0xFF, 0)
            )
            read_coils = _exchange(sock, 2, 1, bytes((1, 0, 8, 0, 8)))
            assert read_coils == bytes((1, 1, 0x10))
            assert _exchange(sock, 3, 1, bytes((6, 0, 100, 0x12, 0x34))) == bytes(
                (6, 0, 100, 0x12, 0x34)
            )
            assert _exchange(sock, 4, 1, bytes((3, 0, 100, 0, 1))) == bytes(
                (3, 2, 0x12, 0x34)
            )
            assert _exchange(sock, 5, 1, bytes((2, 0, 0, 0, 1))) == bytes(
                (0x82, 1)
            )
            assert _exchange(sock, 6, 2, bytes((1, 0, 0, 0, 1))) == bytes(
                (0x81, 11)
            )
    finally:
        server.stop()
    print("plc_simulator: self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description="Modbus TCP PLC 握手模拟器")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="运行 FC01/03/05/06 协议自测后退出",
    )
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0
    PlcSimulatorApp().run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
