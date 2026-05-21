// header.h: 标准系统包含文件的包含文件，
// 或特定于项目的包含文件
//

#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <dxgi.h>
#include <wrl.h>

#include <cassert>
#include <vector>
#include <string>
#include <thread>
#include <functional>

#include <dxgidebug.h>
//#pragma comment(lib, "dxguid.lib")

//// OpenCV（必须全局一致）
#include <opencv2/opencv.hpp>

