# Hardware Integration

## Runtime flow

Hardware adapters live in Core and publish data through the same runtime paths used by
files and built-in tools:

```text
Camera adapter -> HardwareRuntimeService -> FrameSourceState -> ImageState/VisionContext
ToolResultStatus -> HardwareRuntimeService -> PLC tag / Modbus coil / OPC UA node
```

UI code should configure adapters and display status only. It must not call vendor SDKs
or protocol sockets directly.

The application exposes this through `View -> Device connection`. The window configures
camera sources, Modbus TCP coils, Modbus-backed PLC tags, and OPC UA NodeIds without
calling protocol implementations from UI code. `HardwareRuntimeService::Tick()` performs
camera grabs asynchronously and publishes completed frames on the UI thread, so a camera
read does not block ImGui rendering or mutate `ImageState` from a worker thread.

## OpenCV camera adapter

`OpenCvCameraAdapter` supports camera indexes and stream URLs available through the
installed OpenCV video backends.

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

For the application runtime, prefer `HardwareRuntimeService::ConnectCamera()` and enable
automatic capture. Manual `GrabCameraFrame()` remains available for integrations and
tests. Each published frame updates `FrameSourceState`, `ImageState`, `VisionContext`, and
the pending GPU upload image together.

Vendor cameras that expose UVC, RTSP, FFmpeg, or GStreamer can use this adapter. Cameras
requiring a vendor SDK should implement `ICameraAdapter`; captured frames still enter
the application through `HardwareRuntimeService::GrabCameraFrame()`.

## Modbus TCP

`ModbusTcpAdapter` is a concrete Winsock client for Modbus TCP functions:

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

The adapter validates transaction ID, protocol ID, unit ID, MBAP length, function code,
write echoes, byte counts, and Modbus exception responses. Transport failures move the
adapter to `Fault`; a Modbus exception leaves the TCP connection available.

## PLC tags over Modbus

`ModbusPlcAdapter` maps named PLC tags onto Modbus coils or holding registers. This lets
recipes and runtime outputs use tag names without knowing register addresses.

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

Boolean tags use `DeviceValue(bool)`, UInt16 tags use `DeviceValue(int64_t)`, and scaled
register tags use `DeviceValue(double)`. Writes reject incompatible types, zero scales,
and values outside the 16-bit register range.

## Inspection output

```cpp
HardwareOutputBinding output;
output.kind = HardwareOutputKind::PlcTag;
output.adapterKey = "plc-main";
output.target = "inspection.ok";
output.invert = false;

HardwareRuntimeService::PublishInspectionStatus(result.status, output);
```

`Pass` writes true. `Fail` and `Error` write false unless `invert` is enabled.
When automatic publishing is enabled in the device window, `ToolController` aggregates
all non-skipped tool results at the end of each batch. `Error` has priority over `Fail`,
and `Fail` has priority over `Pass`. A preflight failure or missing execution image also
publishes `Error`; skipped/disabled tools do not turn an otherwise passing batch into NG.

## OPC UA

`Open62541OpcUaAdapter` is a native OPC UA TCP client backed by open62541 v1.4.17. It
supports anonymous SecurityPolicy None endpoints, standard text NodeIds such as
`ns=2;s=Inspection.OK`, connection timeouts, and scalar values represented by
`DeviceValue`:

- Boolean
- signed and unsigned 8/16/32/64-bit integers (within `int64_t` range)
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

Integer and floating-point writes first inspect the target node type. For example, an
`int64_t` `DeviceValue` is range-checked and written as Int16 or Int32 when that is the
server node's declared type. Node/type errors keep the session connected; transport,
session, timeout, and SecureChannel errors move the adapter to `Fault`.

The bundled build intentionally has encryption disabled and must not silently connect to
certificate-protected production endpoints. Deployments requiring Sign/Encrypt must
rebuild open62541 with an approved OpenSSL or mbedTLS configuration and provision the
client certificate, private key, trust list, and rejected-certificate policy.

The same adapter rule applies to native industrial-camera SDKs. Add the SDK-specific
implementation in Core, keep its DLLs optional in packaging, and preserve the OpenCV
adapter as the default portable UVC/RTSP path.
