# 硬件接入说明

> 文档同步日期：2026-08-09。相机优先任务、PLC IO 握手、单任务拍照触发、自动重连和常见连接问题已复核；任务“绑定相机”界面现采用不会裁切标签的两列布局。

## 运行数据流

硬件适配器位于 Core 层，并通过与文件和内置工具相同的运行路径发布数据：

```text
Camera adapter -> HardwareRuntimeService -> FrameSourceState -> ImageState/VisionContext
ToolResultStatus -> HardwareRuntimeService -> PLC tag / Modbus coil / OPC UA node
```

UI 只负责配置适配器和显示状态，不直接调用厂商 SDK 或协议套接字。

“设备连接”页负责配置相机、Modbus TCP、PLC 标签和 OPC UA NodeId。`HardwareRuntimeService::Tick()` 异步抓取相机帧，再在 UI 线程发布完成帧，避免相机读取阻塞 ImGui 或从工作线程直接修改 `ImageState`。

## OpenCV 相机适配器

`OpenCvCameraAdapter` 支持 OpenCV 视频后端可访问的相机索引和视频流 URL。

```cpp
auto camera = std::make_unique<OpenCvCameraAdapter>();
DeviceEndpoint endpoint;
endpoint.address = "0";          // Or rtsp://..., http://..., video device URL
endpoint.resource = "dshow";     // any, dshow, msmf, ffmpeg, gstreamer
endpoint.timeoutMs = 1000;

camera->Connect(endpoint);
camera->StartStream();
HardwareAdapterService::SetCamera(std::move(camera));
HardwareRuntimeService::GrabCameraFrame(1000, "line-camera");
```

应用运行时应优先调用 `HardwareRuntimeService::ConnectCamera()`。手动 `GrabCameraFrame()` 保留给集成和测试使用。每个已发布帧会同步更新 `FrameSourceState`、`ImageState`、`VisionContext` 和待上传 GPU 图片。

支持 UVC、RTSP、FFmpeg 或 GStreamer 的相机可直接使用该适配器；必须使用厂商 SDK 的设备应实现 `ICameraAdapter`，抓取结果仍通过 `HardwareRuntimeService` 进入统一图像链。

### 任务相机优先

任务管理中的“使用工业相机（优先）”只设置任务输入优先级，不会自动连接设备。使用前必须先在“设备连接”页成功连接相机。

执行时遵循以下规则：

1. 至少一个本轮启用任务勾选相机优先，并且相机已连接时，请求一张新帧；
2. 抓帧成功后，相机优先任务使用该帧；
3. 抓帧失败时仍完成本次请求，并自动使用任务文件夹、任务单图或公共图片继续执行；
4. 未勾选相机优先的任务继续使用自己的任务图片；
5. 循环执行会在每轮结束后重新请求相机帧。

一次“执行全部”共用一张相机帧，不会为每个任务重复抓帧。任务并行只并行算法管线，不并行访问同一台相机。

### 相机索引 0 打不开

日志 `OpenCV could not open camera source: 0` 表示 OpenCV 当时未能占用索引 0，并不表示任务图片或工具链损坏。建议按以下顺序检查：

1. 在 Windows 设备管理器确认相机存在且状态正常；
2. 关闭系统相机、浏览器视频页面、会议软件或其他可能占用摄像头的程序；
3. 地址保持 `0`，Windows USB 相机优先选择 `dshow`；
4. `dshow` 不可用时尝试 `msmf` 或自动后端；
5. 只点击一次“连接相机”，等待状态更新；连续点击会产生多条独立的失败日志。

相机未连接时，相机优先任务不会反复尝试连接，而是直接使用备用图片。设备自动重连只针对已经成功连接后发生的连续抓帧故障。

## Modbus TCP

`ModbusTcpAdapter` 是基于 Winsock 的 Modbus TCP 客户端，当前支持：

- `01`: read coils
- `03`: read holding registers
- `05`: write one coil
- `06`: write one holding register

```cpp
auto modbus = std::make_unique<ModbusTcpAdapter>();
DeviceEndpoint endpoint;
endpoint.address = "192.168.1.20";
endpoint.port = 502;              // 0 also selects the default 502
endpoint.resource = "1";         // Modbus unit ID, default 1
endpoint.timeoutMs = 1500;

modbus->Connect(endpoint);
HardwareAdapterService::Register("modbus-main", std::move(modbus));
```

适配器会校验事务 ID、协议 ID、Unit ID、MBAP 长度、功能码、写回显、字节数量和 Modbus 异常响应。网络传输失败会把适配器切换到 `Fault`；PLC 返回协议异常时保留 TCP 连接，便于修正地址后继续测试。

### 工业 PLC IO 握手

在“设备连接 → 检测结果输出”中选择“Modbus TCP 线圈”，填写 PLC IP、端口和 Unit ID，然后启用“PLC IO 映射与握手”。启用握手后，旧的单线圈“批次完成后发布”会停用，结果改由下列独立信号发布：

| 信号 | 默认方向 | 行为 |
| --- | --- | --- |
| Trigger | PLC → 视觉 | OFF→ON 上升沿触发；每一行可绑定不同任务 |
| Busy | 视觉 → PLC | 接受触发后置 ON，检测完成后置 OFF |
| Done | 视觉 → PLC | 完成信号；`脉冲时间 > 0` 时自动复位 |
| OK | 视觉 → PLC | 当前任务判定为 Pass 时置 ON |
| NG | 视觉 → PLC | 当前任务判定为 Fail 时置 ON |
| Error | 视觉 → PLC | 相机、任务、算法或超时异常时置 ON |
| Heartbeat | 视觉 → PLC | 按配置周期翻转，供 PLC 判断视觉程序在线 |
| ACK | PLC → 视觉 | PLC 读取结果后的确认上升沿；收到后清除结果输出 |

每行均可配置启用、信号、读写方向、零基协议地址、反相和输出脉冲时间。Trigger 行额外绑定任务名称。参数修改后需要点击“重新连接输出”使运行时配置生效。

任务列表发生变化时，设备页按任务稳定 ID 同步 Trigger 映射：新任务自动补齐 Trigger；任务重命名只更新绑定名称并保留原自定义地址；删除任务会移除对应的旧 Trigger；任务排序不会改写已有绑定。标准地址为任务01使用0，任务02～任务16依次使用8～22；地址1～7保留给 Busy、Done、OK、NG、Error、Heartbeat 和 ACK。“补齐任务 Trigger”可手动再次检查缺项；只有点击“恢复标准映射”时，才会按当前任务顺序重建完整地址表和标准地址。

标准线圈地址如下。地址是 Modbus 协议零基地址；PLC 编程软件若以 `00001` 起始显示，通常需要把表中地址加 1 后对照。

| 地址 | 默认信号/任务 | 地址 | 默认信号/任务 |
| ---: | --- | ---: | --- |
| 0 | 任务01 Trigger | 8 | 任务02 Trigger |
| 1 | Busy | 9 | 任务03 Trigger |
| 2 | Done | 10 | 任务04 Trigger |
| 3 | OK | 11 | 任务05 Trigger |
| 4 | NG | 12 | 任务06 Trigger |
| 5 | Error | 13 | 任务07 Trigger |
| 6 | Heartbeat | 14 | 任务08 Trigger |
| 7 | ACK | 15 | 任务09 Trigger |
| 16 | 任务10 Trigger | 20 | 任务14 Trigger |
| 17 | 任务11 Trigger | 21 | 任务15 Trigger |
| 18 | 任务12 Trigger | 22 | 任务16 Trigger |
| 19 | 任务13 Trigger |  |  |

#### PLC 触发指定任务拍照

1. 在设备页连接工业相机；
2. 在 IO 表为任务添加 Trigger，例如任务01 使用地址 0、任务02 使用地址 8；
3. PLC 先确认 Busy、Done、OK、NG、Error 均为 OFF；
4. PLC 把对应 Trigger 从 OFF 写为 ON；
5. 视觉程序抓取一张新帧，只执行该 Trigger 绑定的任务；其他任务不会执行；
6. PLC 等待 Busy 变为 OFF 且 Done 变为 ON，读取 OK/NG/Error；
7. PLC 把 ACK 从 OFF 写为 ON，视觉程序清除结果输出；
8. PLC 将 Trigger 和 ACK 恢复为 OFF，为下一次上升沿做准备。

PLC Trigger 的输入优先级为“在线相机 → 任务文件夹 → 任务单图 → 公共图片”。相机已连接时，即使任务没有勾选“使用工业相机（优先）”，PLC Trigger 也会抓取新帧；相机未连接时自动执行该任务绑定的文件夹或单图，文件夹每次 Trigger 推进一张。相机已经在线但本轮实际抓帧失败时仍发布 Error，避免掩盖生产相机故障。

Trigger 不使用任务积压队列。视觉空闲时只接受一个上升沿；从请求已接收、Busy 执行、结果发布到等待 ACK 的整个周期内，新的 Trigger 都会被忽略并累计“忙碌触发”次数。ACK 完成且 Busy 释放后才接受下一轮，因此连续触发不会在停止操作后继续补跑和慢慢切换旧图片。PLC 程序应在 Busy=OFF 后再产生下一次 Trigger 上升沿。

推荐的 PLC 侧状态机伪代码：

```text
IF VisionBusy = OFF AND VisionDone = OFF AND RequestPending THEN
    SelectedTaskTrigger := ON;        // 只对目标任务产生一次上升沿
END_IF

IF VisionBusy = ON THEN
    SelectedTaskTrigger := OFF;       // 释放 Trigger，等待本轮完成
END_IF

IF VisionDone = ON THEN
    Read VisionOK / VisionNG / VisionError;
    VisionACK := ON;                  // 确认已经读取结果
END_IF

IF VisionDone = OFF AND VisionBusy = OFF THEN
    VisionACK := OFF;
    RequestPending := OFF;            // 本轮握手结束，才允许下一请求
END_IF
```

不要用固定延时连续发送 Trigger，也不要在 Busy=ON 或 Done=ON 等待 ACK 时切换另一个任务 Trigger。界面中的“Busy 期间已忽略 N 次 Trigger”用于发现这类 PLC 时序错误；被忽略的次数不会在 ACK 后恢复执行。

设备页提供三类联调入口：

- 输入点“读取”、输出点“ON/OFF”：单点检查地址、方向和反相；
- “握手测试 Pass/Fail/Error”：不运行算法，验证整套输出与 ACK；
- “模拟触发：执行当前任务”：不需要 PLC 写 Trigger；有相机时拍照，无相机时使用任务文件夹/单图。

后台线程按轮询周期读取输入，通讯失败达到阈值后执行指数退避自动重连。界面显示最后成功通讯时间、连续失败次数、重连次数、通讯报警、当前握手任务及 ACK 等待状态。检测超时或 ACK 超时会保留明确的握手报警；ACK 超时后自动清除 Busy、Done、OK、NG 和 Error 输出，本轮请求不会补跑，PLC 应在输出复位后重新产生一次有效 Trigger 上升沿。

### 无 PLC 联调模拟器

仓库提供独立的图形化 Modbus TCP PLC 模拟器：`tools/plc_simulator/run_plc_simulator.bat`。它可以分别触发任务01～任务16，实时观察 Busy、Done、OK、NG、Error 和 Heartbeat，并支持手动或自动 ACK。

模拟器默认监听 `127.0.0.1:1502`，Unit ID 为 `1`。主程序使用相同参数连接后，即可在模拟器中点击“触发拍照”验证指定任务。完整地址表、操作步骤和协议自测方法见 [PLC 模拟器说明](../tools/plc_simulator/README.md)。

### TCP 已连接但请求超时

接收响应头时出现 `WSA=10060`，表示 TCP 连接已经建立，但超时时间内没有收到 Modbus 响应。这不是图片工具或检测链错误，应依次检查：

1. PLC 是否在配置端口运行 Modbus TCP Server，通常为 502；端口能连接不等于 Modbus 服务正常；
2. Unit ID 是否正确；直连设备常用 1，部分网关使用 0 或 255；
3. 协议地址是否从 0 开始；PLC 显示线圈 `00001` 通常对应程序地址 0；
4. PLC 是否支持功能码 01 和 05；若目标是保持寄存器，应改用 Modbus PLC 标签映射；
5. 确认服务、Unit ID、地址和功能码后再增加超时；延长超时无法修复 PLC 不响应的问题。

运行错误会包含功能码、Unit ID 和协议地址，可直接与抓包或 PLC 诊断日志对照。

## 普通 TCP 文本输出

目标是普通 TCP Server 或网络调试工具、而不是 Modbus Server 时，在设备页选择“TCP 文本”。Pass 和 Fail 可分别配置发送内容，并可选择追加 CRLF。适配器以完整发送成功作为本次成功，不等待应用层应答。

例如，以下配置会向 `192.168.10.5:5000` 发送可读文本：

```text
Pass content: PASS
Fail content: FAIL
Append CRLF: enabled
```

需要 Modbus TCP 报文封装的 PLC 不能使用此模式，应选择“Modbus TCP 线圈”或基于 Modbus 的 PLC 标签输出。

## 基于 Modbus 的 PLC 标签

`ModbusPlcAdapter` 把命名 PLC 标签映射到 Modbus 线圈或保持寄存器，使配方与运行输出可以使用标签名，而不必直接感知寄存器地址。

```cpp
auto plc = std::make_unique<ModbusPlcAdapter>();

ModbusPlcTagBinding ok;
ok.kind = ModbusPlcTagKind::Coil;
ok.valueType = ModbusPlcValueType::Boolean;
ok.address = 17;
plc->ConfigureTag("inspection.ok", ok);

ModbusPlcTagBinding temperature;
temperature.kind = ModbusPlcTagKind::HoldingRegister;
temperature.valueType = ModbusPlcValueType::ScaledDouble;
temperature.address = 100;
temperature.scale = 0.1;
temperature.offset = -20.0;
plc->ConfigureTag("temperature", temperature);

plc->Connect({"192.168.1.20", 502, "1"});
HardwareAdapterService::Register("plc-main", std::move(plc));
```

布尔标签使用 `DeviceValue(bool)`，UInt16 标签使用 `DeviceValue(int64_t)`，带比例的寄存器标签使用 `DeviceValue(double)`。写入会拒绝不兼容类型、零比例系数以及超出 16 位寄存器范围的值。

## 检测结果输出

```cpp
HardwareOutputBinding output;
output.kind = HardwareOutputKind::PlcTag;
output.adapterKey = "plc-main";
output.target = "inspection.ok";
output.invert = false;

HardwareRuntimeService::PublishInspectionStatus(result.status, output);
```

未反相时，`Pass` 写入 true，`Fail` 和 `Error` 写入 false。设备页启用自动发布后，`ToolController` 在每批结束时聚合所有非跳过工具：`Error` 优先于 `Fail`，`Fail` 优先于 `Pass`。预检失败或缺少执行图像也会发布 `Error`；跳过或禁用工具不会把原本通过的批次变成 NG。

## OPC UA

`Open62541OpcUaAdapter` 是基于 open62541 v1.4.17 的原生 OPC UA TCP 客户端，支持匿名 SecurityPolicy None 端点、`ns=2;s=Inspection.OK` 这类标准文本 NodeId、连接超时，以及由 `DeviceValue` 表示的标量：

- Boolean
- 有符号/无符号 8/16/32/64 位整数（限 `int64_t` 可表示范围）
- Float and Double
- String

```cpp
auto opcUa = std::make_unique<Open62541OpcUaAdapter>();
DeviceEndpoint endpoint;
endpoint.address = "192.168.1.30"; // Full opc.tcp:// URL is also accepted
endpoint.port = 4840;              // 0 selects the OPC UA default 4840
endpoint.resource = "factory";    // Optional endpoint URL path
endpoint.timeoutMs = 1500;

opcUa->Connect(endpoint);
opcUa->WriteNode("ns=2;s=Inspection.OK", DeviceValue(true));
HardwareAdapterService::Register("opcua-main", std::move(opcUa));
```

整数和浮点写入会先检查目标节点类型。例如，服务器节点声明为 Int16 或 Int32 时，`int64_t` 的 `DeviceValue` 会先做范围检查，再按声明类型写入。节点或类型错误保持会话连接；传输、会话、超时和 SecureChannel 错误会把适配器切换到 `Fault`。

随附构建有意关闭加密，不能静默连接要求证书的生产端点。需要 Sign/Encrypt 时，必须用批准的 OpenSSL 或 mbedTLS 配置重新构建 open62541，并配置客户端证书、私钥、信任列表和拒绝证书策略。

原生工业相机 SDK 也遵守相同的适配器边界：SDK 专用实现在 Core 中接入，其 DLL 在发布包中按需携带，同时保留 OpenCV 适配器作为默认可移植 UVC/RTSP 路径。
