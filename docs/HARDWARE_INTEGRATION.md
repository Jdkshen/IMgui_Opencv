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

## OPC UA and vendor SDKs

`IOpcUaAdapter` and adapter lifecycle support are complete, but the repository does not
bundle an OPC UA stack. A production implementation should use the selected deployment
stack, such as open62541, UA-.NETStandard through a process boundary, or the PLC vendor
SDK. The implementation must remain behind `IOpcUaAdapter` so offline recipes and tests
do not depend on a connected server.

The same rule applies to native industrial-camera SDKs. Add the SDK-specific adapter in
Core, keep its DLLs optional in packaging, and preserve the OpenCV adapter as the default
portable path.
