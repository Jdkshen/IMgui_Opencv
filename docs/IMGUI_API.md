# Dear ImGui C++ API 完整参考手册

> 文档同步日期：2026-08-09。API 参考版本仍为项目内 Dear ImGui 1.92.8 WIP；本轮使用表格完成标签/控件对齐，并用独立 ImGui 窗口承载流程图，API 版本未变。


> Dear ImGui v1.92.8 WIP | 基于项目 `include/imgui/imgui.h` 整理
>
> 每个函数标注：**用途** → **签名** → **关键参数** → **代码示例**

---

## 目录

- [一、核心数据类型](#一核心数据类型)
- [二、上下文与帧管理](#二上下文与帧管理)
- [三、窗口 (Windows)](#三窗口-windows)
- [四、子窗口与布局](#四子窗口与布局)
- [五、ID 栈系统](#五id-栈系统)
- [六、文本与标签](#六文本与标签)
- [七、按钮与选择](#七按钮与选择)
- [八、复选框 / 单选框 / 进度条](#八复选框--单选框--进度条)
- [九、Combo / ListBox](#九combo--listbox)
- [十、Drag / Slider / Input](#十drag--slider--input)
- [十一、颜色编辑器](#十一颜色编辑器)
- [十二、树节点与折叠头](#十二树节点与折叠头)
- [十三、弹出框 / 模态框 / 工具提示](#十三弹出框--模态框--工具提示)
- [十四、菜单栏](#十四菜单栏)
- [十五、标签页](#十五标签页)
- [十六、表格 (Tables)](#十六表格-tables)
- [十七、拖放 (Drag & Drop)](#十七拖放-drag--drop)
- [十八、绘图 API (ImDrawList)](#十八绘图-api-imdrawlist)
- [十九、样式系统 (ImGuiStyle)](#十九样式系统-imguistyle)
- [二十、输入输出 (ImGuiIO)](#二十输入输出-imguiio)
- [二十一、字体系统](#二十一字体系统)
- [二十二、工具辅助类](#二十二工具辅助类)
- [二十三、窗口标志速查表](#二十三窗口标志速查表)
- [二十四、完整应用示例](#二十四完整应用示例)

---

## 一、核心数据类型

### 1.1 `ImVec2` — 二维向量

```cpp
struct ImVec2 {
    float x, y;
    ImVec2()                               : x(0), y(0) {}
    ImVec2(float _x, float _y)             : x(_x), y(_y) {}
};
```
**用途:**
- 窗口位置/大小: `GetWindowPos()`, `SetNextWindowSize()`
- 光标坐标: `GetCursorScreenPos()`
- 任何需要坐标/大小的参数

### 1.2 `ImVec4` — 四维向量 (颜色)

```cpp
struct ImVec4 {
    float x, y, z, w;
    ImVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};
```
**用途:** 存储 RGBA 颜色 (x=R, y=G, z=B, w=A)，常用于 `PushStyleColor()`、`TextColored()`、`ImDrawList` 操作。

### 1.3 `ImGuiID` — 唯一标识符

```cpp
typedef unsigned int ImGuiID;  // 32-bit 哈希值
```
**用途:** 窗口/控件的唯一 ID，通过字符串/指针/整数哈希生成。

### 1.4 `ImColor` — 颜色辅助

```cpp
ImColor(int r, int g, int b, int a = 255);
ImColor(ImU32 rgba);  // 0xAABBGGRR
operator ImU32() const;  // 转为 ImU32 供 ImDrawList 使用
```

---

## 二、上下文与帧管理

### 2.1 上下文

```cpp
// 创建/销毁上下文 (通常使用默认即可)
ImGuiContext* CreateContext(ImFontAtlas* shared_font_atlas = NULL);
void          DestroyContext(ImGuiContext* ctx = NULL);
ImGuiContext* GetCurrentContext();
void          SetCurrentContext(ImGuiContext* ctx);
```

### 2.2 每帧核心函数

```cpp
// ——— 主循环结构 ———
// 每帧必须按顺序调用以下函数:

// 1. 通知 ImGui 开始新帧
ImGui::NewFrame();

// 2. 构建 UI (所有窗口/控件在这之间调用)
//    ImGui::Begin() ... ImGui::End()
//    ImGui::Button() ...

// 3. 结束帧，完成几何体生成
ImGui::Render();  // 等价于 EndFrame() + 最终化

// 4. 获取绘制数据并提交给渲染后端
ImDrawData* draw_data = ImGui::GetDrawData();
ImGui_ImplDX12_RenderDrawData(draw_data, command_list);
```

### 2.3 状态访问器

```cpp
ImGuiIO&     GetIO();       // 输入/输出配置 (键盘/鼠标/时间等)
ImGuiStyle&  GetStyle();    // 样式 (颜色/大小/间距等)
ImGuiPlatformIO& GetPlatformIO();  // 平台接口 (剪贴板/IME等)
const char*  GetVersion();  // 版本字符串 "1.92.8 WIP"
```

### 2.4 Demo & 调试窗口

```cpp
void ShowDemoWindow(bool* p_open = NULL);       // 演示窗口 (学习 API 的最佳方式)
void ShowMetricsWindow(bool* p_open = NULL);    // 性能/调试指标
void ShowDebugLogWindow(bool* p_open = NULL);   // 调试日志
void ShowIDStackToolWindow(bool* p_open = NULL);// ID 栈调试工具
void ShowAboutWindow(bool* p_open = NULL);      // 关于窗口
void ShowStyleEditor(ImGuiStyle* ref = NULL);   // 样式编辑器
bool ShowStyleSelector(const char* label);      // 样式选择器
void ShowFontSelector(const char* label);       // 字体选择器
void ShowUserGuide();                           // 用户操作指南
```

### 2.5 预设配色方案

```cpp
void StyleColorsDark(ImGuiStyle* dst = NULL);    // 暗色主题 (默认)
void StyleColorsLight(ImGuiStyle* dst = NULL);   // 亮色主题
void StyleColorsClassic(ImGuiStyle* dst = NULL); // 经典主题
```

---

## 三、窗口 (Windows)

### 3.1 Begin/End — 创建窗口

```cpp
bool Begin(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0);
void End();
```

```cpp
// ——— 基本窗口 ———
bool show = true;
if (ImGui::Begin("My Window", &show)) {
    ImGui::Text("Hello, world!");
    // ... 所有控件放在 Begin/End 之间 ...
}
ImGui::End();  // 必须始终调用，无论 Begin() 返回什么

// ——— 不可关闭的窗口 ———
if (ImGui::Begin("Fixed", NULL, ImGuiWindowFlags_NoCollapse)) {
    ImGui::Text("This window cannot be closed or collapsed.");
}
ImGui::End();
```

### 3.2 窗口状态控制 (Begin 之前调用)

```cpp
// 位置
void SetNextWindowPos(const ImVec2& pos, ImGuiCond cond = 0, const ImVec2& pivot = ImVec2(0,0));
// 大小 (设 0.0f 为自动适应)
void SetNextWindowSize(const ImVec2& size, ImGuiCond cond = 0);
// 大小约束 (最小值、最大值、自定义回调)
void SetNextWindowSizeConstraints(const ImVec2& size_min, const ImVec2& size_max, ...);
// 内容大小 (影响滚动条范围)
void SetNextWindowContentSize(const ImVec2& size);
// 折叠状态
void SetNextWindowCollapsed(bool collapsed, ImGuiCond cond = 0);
// 获得焦点
void SetNextWindowFocus();
// 滚动位置
void SetNextWindowScroll(const ImVec2& scroll);
// 背景透明度
void SetNextWindowBgAlpha(float alpha);
```

**ImGuiCond 条件枚举** (控制何时应用设置):

| 值 | 说明 |
|----|------|
| `ImGuiCond_None` | 无条件 |
| `ImGuiCond_Always` | 始终应用 (默认) |
| `ImGuiCond_Once` | 仅首次应用 |
| `ImGuiCond_FirstUseEver` | 仅程序首次启动 |
| `ImGuiCond_Appearing` | 窗口刚出现时 |

### 3.3 窗口信息查询

```cpp
bool        IsWindowAppearing();          // 窗口正在出现 (第一帧)
bool        IsWindowCollapsed();          // 窗口是否折叠
bool        IsWindowFocused(...);         // 窗口是否有焦点
ImDrawList* GetWindowDrawList();          // 获取当前窗口的绘制列表
float       GetWindowDpiScale();          // 当前窗口 DPI 缩放
ImVec2      GetWindowPos();               // 窗口屏幕坐标 (不推荐使用)
ImVec2      GetWindowSize();              // 窗口大小 (不推荐使用)
float       GetWindowWidth();
float       GetWindowHeight();
ImGuiViewport* GetWindowViewport();       // 窗口所在的 Viewport
```

### 3.4 经典窗口: 可关闭 + 大小可调

```cpp
bool show = true;
ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
if (ImGui::Begin("Settings", &show)) {
    ImGui::Text("Content goes here...");
}
ImGui::End();
```

---

## 四、子窗口与布局

### 4.1 子窗口 (BeginChild / EndChild)

```cpp
// 使用字符串 ID
bool BeginChild(const char* str_id, const ImVec2& size = ImVec2(0,0),
                ImGuiChildFlags child_flags = 0, ImGuiWindowFlags window_flags = 0);
// 使用整数 ID
bool BeginChild(ImGuiID id, const ImVec2& size = ImVec2(0,0),
                ImGuiChildFlags child_flags = 0, ImGuiWindowFlags window_flags = 0);
void EndChild();

// size 约定:
//   0.0f → 使用剩余可用空间
//  >0.0f → 固定大小
//  <0.0f → 从右/下边界对齐
```

```cpp
// ——— 示例: 带边框的子窗口 ———
if (ImGui::BeginChild("LeftPanel", ImVec2(200, 0), ImGuiChildFlags_Borders)) {
    ImGui::Text("Left panel content");
}
ImGui::EndChild();

ImGui::SameLine();  // 水平排列

if (ImGui::BeginChild("RightPanel", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
    ImGui::Text("Right panel content");
}
ImGui::EndChild();
```

### 4.2 布局控制

```cpp
// 分隔线
void Separator();
void SeparatorText(const char* label);  // 带文字的分隔线

// 同行/换行
void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f);
void NewLine();    // 撤销 SameLine()

// 间距
void Spacing();    // 垂直间距
void Dummy(const ImVec2& size);  // 固定大小占位

// 缩进
void Indent(float indent_w = 0.0f);
void Unindent(float indent_w = 0.0f);

// 组 (将多个控件绑定为一个逻辑项)
void BeginGroup();
void EndGroup();

// 文本垂直对齐
void AlignTextToFramePadding();
```

### 4.3 光标操作

```cpp
// 屏幕坐标 (推荐使用)
ImVec2 GetCursorScreenPos();    // 光标绝对坐标
void   SetCursorScreenPos(const ImVec2& pos);

// 窗口本地坐标
ImVec2 GetCursorPos();
float  GetCursorPosX();
float  GetCursorPosY();
void   SetCursorPos(const ImVec2& local_pos);
void   SetCursorPosX(float x);
void   SetCursorPosY(float y);

// 可用空间
ImVec2 GetContentRegionAvail(); // 从当前位置到窗口底部的剩余空间
ImVec2 GetCursorStartPos();     // 初始光标位置
```

### 4.4 文字相关的布局查询

```cpp
float GetTextLineHeight();              // 文字行高 (≈ FontSize)
float GetTextLineHeightWithSpacing();   // 行高 + ItemSpacing.y
float GetFrameHeight();                 // 控件外框高 (≈ FontSize + FramePadding.y*2)
float GetFrameHeightWithSpacing();      // 外框高 + ItemSpacing.y
```

### 4.5 滚动

```cpp
float GetScrollX();  float GetScrollY();     // 当前滚动量
float GetScrollMaxX(); float GetScrollMaxY();// 最大滚动量
void  SetScrollX(float); void SetScrollY(float);
void  SetScrollHereX(float center_ratio = 0.5f); // 滚动到当前光标
void  SetScrollHereY(float center_ratio = 0.5f);
void  SetScrollFromPosX(float local_x, float center_ratio = 0.5f);
void  SetScrollFromPosY(float local_y, float center_ratio = 0.5f);
```

---

## 五、ID 栈系统

> Dear ImGui 通过 ID 区分控件。同名控件需要不同的 ID。

```cpp
void PushID(const char* str_id);           // 推入字符串 ID
void PushID(const char* begin, const char* end);  // 推入字符串范围
void PushID(const void* ptr_id);           // 推入指针 ID
void PushID(int int_id);                   // 推入整数 ID
void PopID();                              // 弹出 ID
ImGuiID GetID(const char* str_id);         // 手动计算 ID (结合当前栈)
```

```cpp
// ——— 正确示例: 用 PushID 区分同名按钮 ———
for (int i = 0; i < 10; i++) {
    ImGui::PushID(i);
    if (ImGui::Button("Delete")) { DeleteItem(i); }  // 每个 "Delete" 有不同 ID
    ImGui::PopID();
}

// ——— 也可用 "##" 隐藏标签后缀 ———
ImGui::Button("Delete##0");
ImGui::Button("Delete##1");   // "##" 后面的部分不显示但参与 ID 计算

// ——— 或用指针作为 ID ———
ImGui::PushID(&item);
ImGui::Button("Delete");
ImGui::PopID();
```

---

## 六、文本与标签

### 6.1 基本文本

```cpp
void TextUnformatted(const char* text, const char* text_end = NULL); // 原始文本 (不解析格式化)
void Text(const char* fmt, ...);        // 格式化文本
void TextV(const char* fmt, va_list);   // va_list 版本
void TextColored(const ImVec4& col, const char* fmt, ...);   // 彩色文本
void TextDisabled(const char* fmt, ...);                       // 灰色文本
void TextWrapped(const char* fmt, ...);                        // 自动换行文本
void LabelText(const char* label, const char* fmt, ...);      // 标签:值 格式文本
void BulletText(const char* fmt, ...);  // 项目符号 + 文本
void Bullet();                           // 仅绘制项目符号
void TextLink(const char* label);        // 超链接文本，点击返回 true
bool TextLinkOpenURL(const char* label, const char* url = NULL);  // 自动打开 URL/文件
```

```cpp
// ——— 示例 ———
ImGui::Text("Frame: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
ImGui::TextColored(ImVec4(1, 1, 0, 1), "Warning: %s", msg);
ImGui::BulletText("Item %d", i);
ImGui::TextLink("Click me for Google");  // 返回 true 时被点击
ImGui::TextLinkOpenURL("Open Readme", "README.md");
```

---

## 七、按钮与选择

### 7.1 按钮

```cpp
bool Button(const char* label, const ImVec2& size = ImVec2(0,0));
bool SmallButton(const char* label);   // 无内边距的小按钮
bool InvisibleButton(const char* str_id, const ImVec2& size, ImGuiButtonFlags flags = 0);
bool ArrowButton(const char* str_id, ImGuiDir dir);  // 方向箭按钮
```

```cpp
if (ImGui::Button("OK", ImVec2(120, 0))) { OnOK(); }
ImGui::SameLine();
if (ImGui::Button("Cancel")) { OnCancel(); }

// 箭头按钮
if (ImGui::ArrowButton("##left", ImGuiDir_Left))  { page--; }
ImGui::SameLine();
if (ImGui::ArrowButton("##right", ImGuiDir_Right)) { page++; }
```

### 7.2 图片显示

```cpp
void Image(ImTextureRef tex, const ImVec2& size, const ImVec2& uv0 = ImVec2(0,0), const ImVec2& uv1 = ImVec2(1,1));
void ImageWithBg(ImTextureRef tex, const ImVec2& size, ..., const ImVec4& bg_col, const ImVec4& tint_col);
bool ImageButton(const char* str_id, ImTextureRef tex, const ImVec2& size, ..., const ImVec4& bg_col, const ImVec4& tint_col);
```

### 7.3 选中 / 悬停 / 焦点 查询

```cpp
bool IsItemHovered(ImGuiHoveredFlags flags = 0);   // 鼠标悬停
bool IsItemActive();     // 控件处于激活状态 (如按钮按下中)
bool IsItemFocused();    // 键盘/手柄聚焦
bool IsItemClicked(ImGuiMouseButton button = 0);   // 刚被点击
bool IsItemVisible();    // 控件可见 (未被裁剪)
bool IsItemEdited();     // 值刚被修改
bool IsItemActivated();  // 刚被激活
bool IsItemDeactivated();// 刚被失活
bool IsItemDeactivatedAfterEdit();  // 失活 + 值被修改
bool IsItemToggledOpen();// 刚被切换开/关 (用于树节点等)
bool IsAnyItemHovered(); // 任意控件被悬停
bool IsAnyItemActive();  // 任意控件处于激活状态
bool IsAnyItemFocused(); // 任意控件被聚焦
```

---

## 八、复选框 / 单选框 / 进度条

```cpp
// 复选框
bool Checkbox(const char* label, bool* v);
bool CheckboxFlags(const char* label, int* flags, int flags_value);
bool CheckboxFlags(const char* label, unsigned int* flags, unsigned int flags_value);

// 单选框
bool RadioButton(const char* label, bool active);          // 独立版本
bool RadioButton(const char* label, int* v, int v_button); // 绑定到 int

// 进度条
void ProgressBar(float fraction, const ImVec2& size = ImVec2(-FLT_MIN, 0), const char* overlay = NULL);
```

```cpp
// ——— 复选框 ———
bool my_bool = true;
ImGui::Checkbox("Enable Feature", &my_bool);

// ——— CheckboxFlags (类似位掩码编辑) ———
enum { FLAG_A = 1<<0, FLAG_B = 1<<1, FLAG_C = 1<<2 };
int flags = FLAG_A | FLAG_C;
ImGui::CheckboxFlags("Option A", &flags, FLAG_A);
ImGui::CheckboxFlags("Option B", &flags, FLAG_B);
ImGui::CheckboxFlags("Option C", &flags, FLAG_C);

// ——— 单选框 ———
int selected = 0;
ImGui::RadioButton("CPU", &selected, 0); ImGui::SameLine();
ImGui::RadioButton("GPU", &selected, 1); ImGui::SameLine();
ImGui::RadioButton("FPGA", &selected, 2);

// ——— 进度条 ———
ImGui::ProgressBar(0.75f, ImVec2(0, 0), "75%");
```

---

## 九、Combo / ListBox

### 9.1 Combo (下拉框)

```cpp
// ——— 方式一: BeginCombo/EndCombo (最灵活) ———
bool BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags = 0);
void EndCombo();

// ——— 方式二: Combo (便捷函数) ———
bool Combo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items = -1);
bool Combo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items = -1);
bool Combo(const char* label, int* current_item, const char*(*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items = -1);
```

```cpp
// ——— 方式一: 灵活版 ———
static int current = 0;
if (ImGui::BeginCombo("Algorithm", items[current])) {
    for (int i = 0; i < IM_ARRAYSIZE(items); i++) {
        bool selected = (current == i);
        if (ImGui::Selectable(items[i], selected))
            current = i;
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}

// ——— 方式二: 简洁版 ———
const char* items[] = {"YOLO", "SSD", "Faster R-CNN"};
static int idx = 0;
ImGui::Combo("Model", &idx, items, IM_ARRAYSIZE(items));

// ——— 方式三: 零分隔字符串 ———
ImGui::Combo("Type", &idx, "Option A\0Option B\0Option C\0");

// ——— 方式四: 回调获取 ———
auto getter = [](void* data, int idx) -> const char* {
    return ((std::string*)data)[idx].c_str();
};
ImGui::Combo("Dynamic", &idx, getter, &my_vector, my_vector.size());
```

### 9.2 ListBox

```cpp
bool ListBox(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items = -1);
bool ListBox(const char* label, int* current_item, const char*(*getter)(void*,int), void* user_data, int items_count, int height_in_items = -1);
```

---

## 十、Drag / Slider / Input

### 10.1 Drag (拖拽数值)

```cpp
bool DragFloat(const char* label, float* v, float v_speed=1.0f, float v_min=0, float v_max=0,
               const char* format="%.3f", ImGuiSliderFlags flags=0);
// v_min >= v_max 表示无边界
bool DragFloat2/3/4(const char* label, float v[N], ...);
bool DragFloatRange2(const char* label, float* v_min, float* v_max, ...);
bool DragInt/DragInt2/3/4(const char* label, int* v, ...);
bool DragIntRange2(const char* label, int* v_min, int* v_max, ...);
bool DragScalar/DragScalarN(const char* label, ImGuiDataType data_type, void* p_data, int components, ...);
```

### 10.2 Slider (滑块)

```cpp
bool SliderFloat(const char* label, float* v, float v_min, float v_max, ...);
bool SliderFloat2/3/4(...);
bool SliderAngle(const char* label, float* v_rad, float v_deg_min=-360, float v_deg_max=+360, ...);
bool SliderInt/SliderInt2/3/4(...);
bool SliderScalar/SliderScalarN(...);
bool VSliderFloat(const char* label, const ImVec2& size, float* v, float v_min, float v_max, ...);
bool VSliderInt(...);
```

```cpp
// ——— Drag 示例 ———
static float value = 0.5f;
ImGui::DragFloat("Threshold", &value, 0.01f, 0.0f, 1.0f, "%.2f");

// ——— Slider 示例 ———
static float angle = 0.0f;
ImGui::SliderAngle("Rotation", &angle);

// ——— 垂直滑块 ———
ImGui::VSliderFloat("##v", ImVec2(18, 160), &v, 0.0f, 1.0f);

// ——— Range ———
static float range_min = 0.0f, range_max = 1.0f;
ImGui::DragFloatRange2("Range", &range_min, &range_max, 0.01f, 0.0f, 1.0f);
```

### 10.3 Input (文本/数值输入)

```cpp
// 文本输入
bool InputText(const char* label, char* buf, size_t buf_size,
               ImGuiInputTextFlags flags=0, ImGuiInputTextCallback callback=NULL, void* user_data=NULL);
bool InputTextMultiline(const char* label, char* buf, size_t buf_size,
                        const ImVec2& size=ImVec2(0,0), ImGuiInputTextFlags flags=0, ...);
bool InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size, ...);

// 数值输入
bool InputFloat/InputFloat2/3/4(const char* label, float* v, ...);
bool InputInt/InputInt2/3/4(const char* label, int* v, ...);
bool InputDouble(const char* label, double* v, ...);
bool InputScalar/InputScalarN(...);
```

```cpp
// ——— 文本输入 ———
static char name[256] = "";
ImGui::InputText("Name", name, IM_ARRAYSIZE(name));

// 带提示
ImGui::InputTextWithHint("Search", "type here...", buf, sizeof(buf));

// 多行
ImGui::InputTextMultiline("Description", buf, sizeof(buf), ImVec2(-FLT_MIN, 100));

// ——— 数值输入 ———
static int count = 42;
ImGui::InputInt("Count", &count);
ImGui::InputFloat3("Position", pos, "%.2f");
```

### 10.4 ImGuiDataType 枚举

| 枚举值 | C++ 类型 |
|--------|---------|
| `ImGuiDataType_S8` | `char` |
| `ImGuiDataType_U8` | `unsigned char` |
| `ImGuiDataType_S16` | `short` |
| `ImGuiDataType_U16` | `unsigned short` |
| `ImGuiDataType_S32` | `int` |
| `ImGuiDataType_U32` | `unsigned int` |
| `ImGuiDataType_S64` | `long long` |
| `ImGuiDataType_U64` | `unsigned long long` |
| `ImGuiDataType_Float` | `float` |
| `ImGuiDataType_Double` | `double` |

---

## 十一、颜色编辑器

```cpp
bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0);
bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0);
bool ColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags = 0);
bool ColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags = 0,
                  const float* ref_col = NULL);
bool ColorButton(const char* desc_id, const ImVec4& col, ImGuiColorEditFlags flags = 0,
                 const ImVec2& size = ImVec2(0,0));
```

```cpp
static float bg_color[4] = { 0.2f, 0.3f, 0.4f, 1.0f };

// 紧凑编辑器
ImGui::ColorEdit3("Tint", bg_color);

// 完整拾色器
ImGui::ColorPicker4("Background", bg_color,
    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB);

// 颜色按钮
if (ImGui::ColorButton("##btn", ImVec4(1,0,0,1))) { /* clicked */ }
```

---

## 十二、树节点与折叠头

### 12.1 树节点

```cpp
bool TreeNode(const char* label);            // 简单节点
bool TreeNode(const char* fmt, ...);          // 格式化标签
bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags = 0);
void TreePush(const char* str_id);            // 手动推入树层级 (使用 GetID)
void TreePop();                               // 手动弹出
```

```cpp
// ——— 基本树 ———
if (ImGui::TreeNode("Settings")) {
    static bool opt1 = true, opt2 = false;
    ImGui::Checkbox("Option 1", &opt1);
    ImGui::Checkbox("Option 2", &opt2);
    ImGui::TreePop();  // 必须调用
}

// ——— 可选中的叶子节点 ———
if (ImGui::TreeNodeEx("Item 1", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Selected)) {
    ImGui::TreePop();
}

// ——— 带数据的节点 (PushID 区分) ———
for (int i = 0; i < 5; i++) {
    ImGui::PushID(i);
    if (ImGui::TreeNode("Node")) {
        ImGui::Text("Content %d", i);
        ImGui::TreePop();
    }
    ImGui::PopID();
}
```

### 12.2 折叠头 (Collapsing Header)

```cpp
bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0);
bool CollapsingHeader(const char* label, bool* p_visible, ImGuiTreeNodeFlags flags = 0);
```

```cpp
// ——— 简单可折叠区域 ———
if (ImGui::CollapsingHeader("Advanced Options")) {
    ImGui::SliderFloat("Param", &param, 0, 1);
}

// ——— 带关闭按钮 ———
static bool visible = true;
if (ImGui::CollapsingHeader("Section", &visible)) {
    ImGui::Text("Content...");
} // visible=false 时整个区域消失
```

---

## 十三、弹出框 / 模态框 / 工具提示

### 13.1 Popup (弹出窗口)

```cpp
bool BeginPopup(const char* str_id, ImGuiWindowFlags flags = 0);
bool BeginPopupModal(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0);
bool BeginPopupContextItem(const char* str_id = NULL, ImGuiPopupFlags flags = 1);  // 右键菜单
bool BeginPopupContextWindow(const char* str_id = NULL, ImGuiPopupFlags flags = 1);
bool BeginPopupContextVoid(const char* str_id = NULL, ImGuiPopupFlags flags = 1);
void EndPopup();
void OpenPopup(const char* str_id, ImGuiPopupFlags flags = 0);
void CloseCurrentPopup();
```

```cpp
// ——— 简单弹出 ———
if (ImGui::Button("Open Popup"))
    ImGui::OpenPopup("my_popup");

if (ImGui::BeginPopup("my_popup")) {
    ImGui::Text("Popup content");
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ——— 右键上下文菜单 ———
if (ImGui::BeginPopupContextItem("item_context")) {
    if (ImGui::Selectable("Copy"))  { /* ... */ }
    if (ImGui::Selectable("Paste")) { /* ... */ }
    ImGui::EndPopup();
}

// ——— 模态对话框 ———
static bool modal_open = false;
if (ImGui::Button("Open Modal"))
    ImGui::OpenPopup("Confirm");
if (ImGui::BeginPopupModal("Confirm", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Are you sure?");
    if (ImGui::Button("Yes")) { DoIt(); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("No"))  { ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}
```

### 13.2 Tooltip (工具提示)

```cpp
void BeginTooltip();   // 必须在 BeginTooltip/EndTooltip 之内
void EndTooltip();
void SetTooltip(const char* fmt, ...);  // 为上一个控件设置工具提示 (便捷)
bool BeginItemTooltip();  // 当上一项被悬停时开始工具提示
```

```cpp
// ——— 方式一: SetTooltip (推荐) ———
ImGui::Button("Hover me");
ImGui::SetTooltip("This button does something amazing!");

// ——— 方式二: BeginTooltip (手动控制) ———
if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Custom tooltip content");
    ImGui::EndTooltip();
}
```

---

## 十四、菜单栏

```cpp
bool BeginMenuBar();  void EndMenuBar();
bool BeginMainMenuBar(); void EndMainMenuBar();
bool BeginMenu(const char* label, bool enabled = true); void EndMenu();
bool MenuItem(const char* label, const char* shortcut = NULL, bool selected = false, bool enabled = true);
bool MenuItem(const char* label, const char* shortcut, bool* p_selected, bool enabled = true);
```

```cpp
// ——— 主菜单栏 (通常在窗口最顶部) ———
if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O")) { OnOpen(); }
        if (ImGui::MenuItem("Save", "Ctrl+S"))    { OnSave(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4"))    { OnExit(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Copy", "Ctrl+C"))  { OnCopy(); }
        if (ImGui::MenuItem("Paste", "Ctrl+V")) { OnPaste(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        static bool show_log = false;
        ImGui::MenuItem("Log Window", NULL, &show_log);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// ——— 窗口内菜单栏 ———
if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("Options")) {
        ImGui::MenuItem("Settings");
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}
```

---

## 十五、标签页

```cpp
bool BeginTabBar(const char* str_id, ImGuiTabBarFlags flags = 0);
void EndTabBar();
bool BeginTabItem(const char* label, bool* p_open = NULL, ImGuiTabItemFlags flags = 0);
void EndTabItem();
void SetTabItemClosed(const char* tab_or_docked_window_label);
```

```cpp
// ——— 标签页示例 ———
if (ImGui::BeginTabBar("MyTabBar")) {
    if (ImGui::BeginTabItem("Image")) {
        ImGui::Text("Image content here...");
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Video")) {
        ImGui::Text("Video controls here...");
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Settings")) {
        ImGui::Checkbox("Enable Feature", &opt);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
```

---

## 十六、表格 (Tables)

> v1.80+ 新增。提供完整的表格功能: 排序、列宽调整、合并单元格等。

```cpp
bool BeginTable(const char* str_id, int column, ImGuiTableFlags flags = 0,
                const ImVec2& outer_size = ImVec2(0,0), float inner_width = 0.0f);
void EndTable();
void TableNextRow(ImGuiTableRowFlags row_flags = 0, float min_row_height = 0.0f);
bool TableSetColumnIndex(int column_n); // 切到指定列
bool TableNextColumn();                 // 切到下一列
int  TableGetColumnCount();
int  TableGetColumnIndex();
int  TableGetRowIndex();
const char* TableGetColumnName(int column_n = -1);
ImGuiTableColumnFlags TableGetColumnFlags(int column_n = -1);
void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags = 0, float init_width_or_weight = 0.0f, ImGuiID user_id = 0);
void TableSetupScrollFreeze(int cols, int rows);
void TableHeadersRow();   // 自动生成表头
void TableHeader(const char* label);
ImGuiTableSortSpecs* TableGetSortSpecs();
void TableSetBgColor(ImGuiTableBgTarget target, ImU32 color, int column_n = -1);
```

```cpp
// ——— 基本表格 ———
if (ImGui::BeginTable("table", 3,
    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    // 设置列
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Score");
    ImGui::TableSetupColumn("Status");
    ImGui::TableHeadersRow();

    for (const auto& row : data) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("%s", row.name);
        ImGui::TableSetColumnIndex(1); ImGui::Text("%d", row.score);
        ImGui::TableSetColumnIndex(2); ImGui::Text("%s", row.status);
    }
    ImGui::EndTable();
}

// ——— 可排序列 ———
ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort);
ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_PreferSortDescending);

if (ImGuiTableSortSpecs* sorts = ImGui::TableGetSortSpecs()) {
    // sorts->Specs 包含排序信息
}
```

---

## 十七、拖放 (Drag & Drop)

```cpp
// 拖放源
bool BeginDragDropSource(ImGuiDragDropFlags flags = 0);
bool SetDragDropPayload(const char* type, const void* data, size_t sz, ImGuiCond cond = 0);
void EndDragDropSource();

// 拖放目标
bool BeginDragDropTarget();
const ImGuiPayload* AcceptDragDropPayload(const char* type, ImGuiDragDropFlags flags = 0);
void EndDragDropTarget();
```

```cpp
// ——— 拖放源 ———
if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
    ImGui::SetDragDropPayload("MY_ITEM", &item_id, sizeof(int));
    ImGui::Text("Dragging item %d", item_id);
    ImGui::EndDragDropSource();
}

// ——— 拖放目标 ———
if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MY_ITEM")) {
        int item_id = *(const int*)payload->Data;
        OnDrop(item_id);
    }
    ImGui::EndDragDropTarget();
}
```

---

## 十八、绘图 API (ImDrawList)

> 通过 `ImGui::GetWindowDrawList()` 获取当前窗口的绘制列表。

### 18.1 基本形状

```cpp
// 直线 & 折线
void AddLine(const ImVec2& p1, const ImVec2& p2, ImU32 col, float thickness = 1.0f);
void AddPolyline(const ImVec2* points, int num_points, ImU32 col, ImDrawFlags flags, float thickness);

// 矩形
void AddRect(const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding=0, ImDrawFlags flags=0, float thickness=1.0f);
void AddRectFilled(const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding=0, ImDrawFlags flags=0);

// 圆形 & 椭圆
void AddCircle(const ImVec2& center, float radius, ImU32 col, int num_segments=0, float thickness=1.0f);
void AddCircleFilled(const ImVec2& center, float radius, ImU32 col, int num_segments=0);

// 文本
void AddText(const ImVec2& pos, ImU32 col, const char* text, const char* text_end=NULL);
void AddText(const ImFont* font, float font_size, const ImVec2& pos, ImU32 col, const char* text, ...);

// 三角形
void AddTriangle(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 col, float thickness=1.0f);
void AddTriangleFilled(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 col);

// 四边形
void AddQuad(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, const ImVec2& p4, ImU32 col, float thickness=1.0f);
void AddQuadFilled(...);

// 贝塞尔曲线
void AddBezierCubic(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, const ImVec2& p4, ImU32 col, float thickness, int num_segments=0);
void AddBezierQuadratic(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 col, float thickness, int num_segments=0);
```

**典型用法:**

```cpp
ImDrawList* draw = ImGui::GetWindowDrawList();

// 画一个红色矩形框
draw->AddRect(ImVec2(100, 100), ImVec2(300, 200), IM_COL32(255, 0, 0, 255));

// 画一个半透明蓝色填充圆
draw->AddCircleFilled(ImVec2(200, 150), 50, IM_COL32(0, 100, 255, 100));

// 画文本
draw->AddText(ImVec2(10, 10), IM_COL32(255, 255, 255, 255), "Hello DrawList");

// 在图像预览区域画 ROI 框 (项目实际场景)
draw->AddRect(roi_min, roi_max, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
```

### 18.2 图片绘制

```cpp
void AddImage(ImTextureRef tex, const ImVec2& p_min, const ImVec2& p_max,
              const ImVec2& uv_min = ImVec2(0,0), const ImVec2& uv_max = ImVec2(1,1), ImU32 col = IM_COL32_WHITE);
void AddImageQuad(...);
void AddImageRounded(ImTextureRef tex, const ImVec2& p_min, const ImVec2& p_max,
                     const ImVec2& uv_min, const ImVec2& uv_max, ImU32 col, float rounding, ImDrawFlags flags=0);
```

### 18.3 路径 API

```cpp
void PathClear();
void PathLineTo(const ImVec2& pos);
void PathLineToMergeDuplicate(const ImVec2& pos);
void PathArcTo(const ImVec2& center, float radius, float a_min, float a_max, int num_segments=0);
void PathArcToFast(const ImVec2& center, float radius, int a_min_of_12, int a_max_of_12);
void PathBezierCubicCurveTo(const ImVec2& p2, const ImVec2& p3, const ImVec2& p4, int num_segments=0);
void PathBezierQuadraticCurveTo(const ImVec2& p2, const ImVec2& p3, int num_segments=0);
void PathRect(const ImVec2& rect_min, const ImVec2& rect_max, float rounding=0, ImDrawFlags flags=0);
void PathStroke(ImU32 col, ImDrawFlags flags=0, float thickness=1.0f);
void PathFillConvex(ImU32 col);  // 填充凸多边形
void PathFillConcave(ImU32 col); // 填充凹多边形 (更慢)
```

### 18.4 状态管理

```cpp
void PrimReserve(int idx_count, int vtx_count);
void PrimUnreserve(int idx_count, int vtx_count);
void PrimQuadUV(...);   // 直接添加四边形
void PrimRect(const ImVec2& a, const ImVec2& b, ImU32 col);
void PrimRectUV(const ImVec2& a, const ImVec2& b, const ImVec2& uv_a, const ImVec2& uv_b, ImU32 col);

// 裁剪
void PushClipRect(const ImVec2& clip_rect_min, const ImVec2& clip_rect_max, bool intersect_with_current_clip_rect = false);
void PushClipRectFullScreen();
void PopClipRect();
```

---

## 十九、样式系统 (ImGuiStyle)

### 19.1 运行时样式修改

```cpp
// 颜色 (推荐方式: Push/Pop，自动恢复)
void PushStyleColor(ImGuiCol idx, ImU32 col);
void PushStyleColor(ImGuiCol idx, const ImVec4& col);
void PopStyleColor(int count = 1);

// 尺寸变量
void PushStyleVar(ImGuiStyleVar idx, float val);
void PushStyleVar(ImGuiStyleVar idx, const ImVec2& val);
void PushStyleVarX(ImGuiStyleVar idx, float val_x);  // 只改 X 分量
void PushStyleVarY(ImGuiStyleVar idx, float val_y);  // 只改 Y 分量
void PopStyleVar(int count = 1);

// 控件标志
void PushItemFlag(ImGuiItemFlags option, bool enabled);
void PopItemFlag();
```

```cpp
// ——— 设置控件宽度 ———
ImGui::PushItemWidth(200.0f);
ImGui::InputFloat("X", &x);
ImGui::PopItemWidth();

// ——— 临时修改颜色 ———
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));  // 红色文字
ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 200, 0, 255)); // 绿色按钮
ImGui::Button("Important");
ImGui::PopStyleColor(2);  // 恢复两个颜色

// ——— 修改下一项宽度 ———
ImGui::SetNextItemWidth(100.0f);
ImGui::InputFloat("Narrow", &value);

// ——— 文字换行 ———
ImGui::PushTextWrapPos(200.0f);
ImGui::TextWrapped("This long text will wrap at 200 pixels...");
ImGui::PopTextWrapPos();
```

### 19.2 获取样式值

```cpp
ImU32  GetColorU32(ImGuiCol idx, float alpha_mul = 1.0f);  // 获取颜色 (含 Alpha，转 ImU32)
ImU32  GetColorU32(const ImVec4& col);
const ImVec4& GetStyleColorVec4(ImGuiCol idx);  // 获取原始 Vec4 颜色
ImVec2 GetFontTexUvWhitePixel();  // 获取白色像素的 UV 坐标
```

### 19.3 常用颜色枚举 (ImGuiCol_)

| 颜色枚举 | 用途 |
|----------|------|
| `ImGuiCol_Text` | 普通文字颜色 |
| `ImGuiCol_TextDisabled` | 禁用文字颜色 |
| `ImGuiCol_WindowBg` | 窗口背景 |
| `ImGuiCol_ChildBg` | 子窗口背景 |
| `ImGuiCol_FrameBg` | 输入框背景 |
| `ImGuiCol_Button` | 按钮颜色 |
| `ImGuiCol_ButtonHovered` | 按钮悬停 |
| `ImGuiCol_ButtonActive` | 按钮按下 |
| `ImGuiCol_Header` | 折叠头/表头颜色 |
| `ImGuiCol_CheckMark` | 复选框勾选标记 |
| `ImGuiCol_SliderGrab` | 滑块把手 |
| `ImGuiCol_Tab` / `TabHovered` / `TabActive` | 标签页颜色 |
| `ImGuiCol_TitleBg` / `TitleBgActive` | 标题栏 |
| `ImGuiCol_ScrollbarBg` / `ScrollbarGrab` | 滚动条 |
| `ImGuiCol_Border` | 边框颜色 |

完整列表 (共 ~50+ 个) 见 `imgui.h` 中的 `ImGuiCol_` 枚举。

### 19.4 常用样式变量 (ImGuiStyleVar_)

| 枚举 | 类型 | 说明 |
|------|------|------|
| `ImGuiStyleVar_Alpha` | float | 全局透明度 |
| `ImGuiStyleVar_WindowPadding` | ImVec2 | 窗口内边距 |
| `ImGuiStyleVar_WindowRounding` | float | 窗口圆角 |
| `ImGuiStyleVar_FramePadding` | ImVec2 | 控件外框内边距 |
| `ImGuiStyleVar_FrameRounding` | float | 控件外框圆角 |
| `ImGuiStyleVar_ItemSpacing` | ImVec2 | 控件间距 |
| `ImGuiStyleVar_ItemInnerSpacing` | ImVec2 | 控件内部间距 |
| `ImGuiStyleVar_IndentSpacing` | float | 缩进宽度 |
| `ImGuiStyleVar_ScrollbarSize` | float | 滚动条宽度 |
| `ImGuiStyleVar_GrabMinSize` | float | 拖拽把手最小尺寸 |
| `ImGuiStyleVar_GrabRounding` | float | 拖拽把手圆角 |
| `ImGuiStyleVar_TabRounding` | float | 标签圆角 |

---

## 二十、输入输出 (ImGuiIO)

> 通过 `ImGui::GetIO()` 访问。

### 20.1 关键配置字段

```cpp
ImGuiIO& io = ImGui::GetIO();

// 显示
io.DisplaySize = ImVec2(1920, 1080);          // 主显示区域大小 (像素)
io.DisplayFramebufferScale = ImVec2(1, 1);    // 视网膜屏缩放

// 帧时间
io.DeltaTime = 1.0f / 60.0f;                  // 帧间隔 (秒)

// 字体
io.Fonts = my_atlas;                           // 字体图集
io.FontDefault = my_font;                      // 默认字体
io.FontAllowUserScaling = true;               // 允许 Ctrl+滚轮缩放

// 导航 (键盘/手柄)
io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 启用键盘导航
io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // 启用手柄导航

// Docking (窗口停靠)
io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

// Multi-Viewport (多窗口独立显示)
io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

// 鼠标
io.MouseDrawCursor = false;                    // ImGui 是否绘制鼠标光标
io.ConfigMacOSXBehaviors = true;              // macOS 风格行为

// ini 文件
io.IniFilename = "imgui.ini";                 // 窗口布局持久化文件, NULL 禁用
io.IniSavingRate = 5.0f;                       // 最小保存间隔 (秒)

// 日志
io.LogFilename = "imgui_log.txt";
```

### 20.2 输入事件 (由后端调用)

```cpp
// 键盘
io.AddKeyEvent(ImGuiKey_Space, true);         // 按下
io.AddKeyEvent(ImGuiKey_Space, false);         // 松开

// 鼠标
io.AddMousePosEvent(x, y);
io.AddMouseButtonEvent(0, true);               // 左键按下
io.AddMouseButtonEvent(0, false);
io.AddMouseWheelEvent(wheel_x, wheel_y);

// 字符
io.AddInputCharacter('A');
io.AddInputCharactersUTF8("Hello");

// 焦点
io.AddFocusEvent(true);  // 获得焦点
io.AddFocusEvent(false); // 失去焦点
```

### 20.3 输出状态 (读取，由 ImGui 设置)

```cpp
// 鼠标/键盘捕获
io.WantCaptureMouse      // true → 不向游戏/应用传递鼠标事件
io.WantCaptureKeyboard   // true → 不向游戏/应用传递键盘事件
io.WantTextInput         // true → 弹出屏幕键盘 (移动端)

// 导航状态
io.NavActive             // 键盘/手柄导航激活中
io.NavVisible            // 导航高亮可见

// 性能指标
io.Framerate             // 帧率 (60帧滚动平均)
io.MetricsRenderVertices // 顶点数
io.MetricsRenderIndices  // 索引数
io.MetricsRenderWindows  // 可见窗口数
```

### 20.4 键盘查询 (IsKeyXXX)

```cpp
bool IsKeyDown(ImGuiKey key);          // 按键是否按下
bool IsKeyPressed(ImGuiKey key, bool repeat = true);  // 刚按下
bool IsKeyReleased(ImGuiKey key);      // 刚松开
bool IsKeyChordPressed(ImGuiKeyChord key_chord);  // 组合键 (Ctrl+S 等)
float GetKeyData(ImGuiKey key)->AnalogValue;  // 模拟值 (手柄扳机 0~1)

// 修饰键状态
bool IsCtrlDown();    // io.KeyCtrl
bool IsShiftDown();   // io.KeyShift
bool IsAltDown();     // io.KeyAlt
bool IsSuperDown();   // io.KeySuper (Windows键)
bool IsModShortcutNone(); // 是否无修饰键
```

### 20.5 鼠标查询

```cpp
bool IsMouseDown(ImGuiMouseButton button);     // 鼠标键按下中
bool IsMouseClicked(ImGuiMouseButton button, bool repeat = false);
bool IsMouseDoubleClicked(ImGuiMouseButton button);
bool IsMouseReleased(ImGuiMouseButton button);
bool IsMouseDragging(ImGuiMouseButton button, float lock_threshold = -1.0f);
bool IsMouseHoveringRect(const ImVec2& r_min, const ImVec2& r_max, bool clip = true);
bool IsMousePosValid(const ImVec2* mouse_pos = NULL);
ImVec2 GetMousePos();
ImVec2 GetMousePosOnOpeningCurrentPopup();  // 弹出窗口打开时的鼠标位置
ImVec2 GetMouseDragDelta(ImGuiMouseButton button, float lock_threshold = -1.0f);
void   ResetMouseDragDelta(ImGuiMouseButton button);
```

### 20.6 禁用/启用控件

```cpp
void BeginDisabled(bool disabled = true);
void EndDisabled();
```

```cpp
ImGui::BeginDisabled(is_processing);
ImGui::Button("Submit");  // 灰掉的按钮
ImGui::EndDisabled();
```

---

## 二十一、字体系统

### 21.1 加载字体

```cpp
// 获取字体图集
ImFontAtlas* atlas = ImGui::GetIO().Fonts;

// 添加字体 (返回 ImFont*)
ImFont* font = atlas->AddFontFromFileTTF("path/to/font.ttf", size_pixels,
    NULL,  // ImFontConfig* (NULL = 默认)
    atlas->GetGlyphRangesDefault());  // 字形范围

// 加载中文字体 (推荐)
ImFontConfig config;
config.MergeMode = false;  // 首字体不合并
ImFont* cn_font = atlas->AddFontFromFileTTF("simsun.ttc", 20.0f,
    &config, atlas->GetGlyphRangesChineseFull());

// 合并图标字体
config.MergeMode = true;
config.GlyphMinAdvanceX = 20.0f;  // 图标固定宽度
static const ImWchar icon_ranges[] = { 0xE000, 0xE900, 0 };  // Icon 字形范围
atlas->AddFontFromFileTTF("fontawesome.ttf", 20.0f,
    &config, icon_ranges);

// 编译字体图集到 GPU 纹理
atlas->Build();
```

### 21.2 运行时切换字体

```cpp
ImGui::PushFont(bold_font);
ImGui::Text("Bold text");
ImGui::PopFont();

ImFont* current = ImGui::GetFont();
float  size = ImGui::GetFontSize();
```

### 21.3 字体大小控制

```cpp
// 默认字体大小
ImGui::PushFont(NULL, 24.0f);    // 保持当前字体，改大小
ImGui::Text("Bigger text");
ImGui::PopFont();

// 全局字体缩放
ImGui::GetStyle().FontScaleMain = 1.0f;
// 响应 DPI 变化
ImGui::GetIO().ConfigDpiScaleFonts = true;
```

---

## 二十二、工具辅助类

### 22.1 ImGuiListClipper — 大列表虚拟滚动

```cpp
ImGuiListClipper clipper;
clipper.Begin(10000);  // 总共 10000 项
while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        ImGui::Text("Item %d", i);
    }
}
clipper.End();
```

### 22.2 ImGuiOnceUponAFrame — 每帧只执行一次

```cpp
static ImGuiOnceUponAFrame oaf;
if (oaf)
    ImGui::Text("This appears at most once per frame");
```

### 22.3 ImGuiTextFilter — 文本过滤

```cpp
static ImGuiTextFilter filter;
filter.Draw("Filter");  // 绘制过滤输入框
if (filter.PassFilter("some text")) { ... }  // 检查文本是否通过
```

### 22.4 ImGuiTextBuffer — 文本缓冲

```cpp
ImGuiTextBuffer log;
log.append("Line 1\n");
log.appendf("Value: %.2f\n", 3.14f);
ImGui::TextUnformatted(log.begin(), log.end());
```

### 22.5 ImGuiStorage — 键值存储

```cpp
ImGuiStorage storage;
storage.SetFloat(ImGui::GetID("my_float"), 3.14f);
float v = storage.GetFloat(ImGui::GetID("my_float"), 0.0f);
```

### 22.6 剪贴板

```cpp
const char* text = ImGui::GetClipboardText();
ImGui::SetClipboardText("copied text");
ImGui::GetPlatformIO().Platform_SetClipboardTextFn(NULL, "text");
```

### 22.7 其他工具函数

```cpp
// 计算文本宽度
float CalcTextSize(const char* text, ...);
ImVec2 CalcTextSize(const char* text, const char* text_end = NULL,
                    bool hide_text_after_double_hash = false, float wrap_width = -1.0f);

// 帧计数
int GetFrameCount();  // 自程序开始以来的帧数

// 时间
double GetTime();     // 绝对时间 (秒)

// 获取绘制上下文
ImDrawList* GetBackgroundDrawList();  // 背景层 (在所有窗口之后)
ImDrawList* GetForegroundDrawList();  // 前景层 (在所有窗口之前)
ImDrawListSharedData* GetDrawListSharedData();

// 窗口 DrawList
ImDrawList* GetWindowDrawList();      // 当前窗口的 DrawList

// 快捷键
bool Shortcut(ImGuiKeyChord key_chord, ImGuiInputFlags flags = 0);

// 静态工具函数
bool IsKeyChordPressed(ImGuiKeyChord key_chord);  // 如 ImGuiMod_Ctrl | ImGuiKey_S
```

---

## 二十三、窗口标志速查表

### 23.1 ImGuiWindowFlags

| 标志 | 说明 |
|------|------|
| `ImGuiWindowFlags_NoTitleBar` | 隐藏标题栏 |
| `ImGuiWindowFlags_NoResize` | 禁止调整大小 |
| `ImGuiWindowFlags_NoMove` | 禁止移动 |
| `ImGuiWindowFlags_NoScrollbar` | 隐藏滚动条 |
| `ImGuiWindowFlags_NoScrollWithMouse` | 禁用鼠标滚轮滚动 |
| `ImGuiWindowFlags_NoCollapse` | 禁止折叠 |
| `ImGuiWindowFlags_AlwaysAutoResize` | 自动适应内容 |
| `ImGuiWindowFlags_NoBackground` | 不绘制背景 |
| `ImGuiWindowFlags_NoSavedSettings` | 不保存到 ini |
| `ImGuiWindowFlags_NoMouseInputs` | 不接收鼠标输入 |
| `ImGuiWindowFlags_MenuBar` | 有菜单栏 |
| `ImGuiWindowFlags_HorizontalScrollbar` | 显示水平滚动条 |
| `ImGuiWindowFlags_NoFocusOnAppearing` | 出现时不获取焦点 |
| `ImGuiWindowFlags_NoBringToFrontOnFocus` | 选中时不前置 |
| `ImGuiWindowFlags_AlwaysVerticalScrollbar` | 始终显示垂直滚动条 |
| `ImGuiWindowFlags_AlwaysHorizontalScrollbar` | 始终显示水平滚动条 |
| `ImGuiWindowFlags_NoNavInputs` | 不接收导航输入 |
| `ImGuiWindowFlags_NoNavFocus` | 不可被导航选中 |
| `ImGuiWindowFlags_UnsavedDocument` | 标题栏显示修改标记 `*` |
| `ImGuiWindowFlags_NoDocking` | 禁止停靠 |

### 23.2 ImGuiInputTextFlags

| 标志 | 说明 |
|------|------|
| `ImGuiInputTextFlags_CharsDecimal` | 仅允许数字 |
| `ImGuiInputTextFlags_CharsHexadecimal` | 仅允许十六进制 |
| `ImGuiInputTextFlags_CharsUppercase` | 强制大写 |
| `ImGuiInputTextFlags_CharsNoBlank` | 不允许空白 |
| `ImGuiInputTextFlags_AutoSelectAll` | 聚焦时自动全选 |
| `ImGuiInputTextFlags_EnterReturnsTrue` | Enter 键返回 true |
| `ImGuiInputTextFlags_Password` | 密码模式 (显示 *) |
| `ImGuiInputTextFlags_ReadOnly` | 只读 |
| `ImGuiInputTextFlags_CallbackCompletion` | Tab 补全回调 |
| `ImGuiInputTextFlags_CallbackHistory` | 上下键历史回调 |
| `ImGuiInputTextFlags_CallbackAlways` | 每帧回调 |
| `ImGuiInputTextFlags_CallbackCharFilter` | 字符过滤回调 |
| `ImGuiInputTextFlags_CtrlEnterForNewLine` | Ctrl+Enter 换行 (多行模式) |
| `ImGuiInputTextFlags_NoHorizontalScroll` | 不水平滚动 |

### 23.3 ImGuiTreeNodeFlags

| 标志 | 说明 |
|------|------|
| `ImGuiTreeNodeFlags_Selected` | 选中状态 |
| `ImGuiTreeNodeFlags_Framed` | 带外框 |
| `ImGuiTreeNodeFlags_Leaf` | 叶节点 (无展开/折叠) |
| `ImGuiTreeNodeFlags_Bullet` | 项目符号 |
| `ImGuiTreeNodeFlags_DefaultOpen` | 默认展开 |
| `ImGuiTreeNodeFlags_OpenOnDoubleClick` | 双击展开 |
| `ImGuiTreeNodeFlags_OpenOnArrow` | 仅箭头点击展开 |
| `ImGuiTreeNodeFlags_SpanAvailWidth` | 占满可用宽度 |
| `ImGuiTreeNodeFlags_SpanFullWidth` | 占满整行宽度 |
| `ImGuiTreeNodeFlags_NavLeftJumpsBackHere` | 导航左键返回父节点 |

---

## 二十四、完整应用示例

### 24.1 最小程序框架

```cpp
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

int main() {
    // ——— 1. 初始化 Dear ImGui ———
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // 加载字体
    io.Fonts->AddFontFromFileTTF("c:/Windows/Fonts/msyh.ttc", 20.0f,
        NULL, io.Fonts->GetGlyphRangesChineseFull());

    // ——— 2. 初始化平台/渲染后端 ———
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(device, NUM_FRAMES_IN_FLIGHT,
        DXGI_FORMAT_R8G8B8A8_UNORM, srv_desc_heap,
        srv_desc_heap->GetCPUDescriptorHandleForHeapStart(),
        srv_desc_heap->GetGPUDescriptorHandleForHeapStart());

    // ——— 3. 主循环 ———
    while (running) {
        // 消息处理...
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // 开始 ImGui 帧
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // === 构建 UI ===
        BuildUI();  // 所有 ImGui::XXX 调用在这里

        // 结束帧并渲染
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);

        // Present...
    }

    // ——— 4. 清理 ———
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
```

### 24.2 带 Docking 的主界面

```cpp
void BuildUI() {
    // 主菜单栏
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("Exit");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // 全窗口停靠空间
    ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

    // 侧边栏
    ImGui::Begin("Sidebar");
    ImGui::Text("Controls");
    static float threshold = 0.5f;
    ImGui::SliderFloat("Threshold", &threshold, 0.0f, 1.0f);
    if (ImGui::Button("Process")) { OnProcess(); }
    ImGui::End();

    // 图像查看器
    ImGui::Begin("ImageViewer");
    ImGui::Text("Image area...");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    draw->AddRectFilled(pos, ImVec2(pos.x+640, pos.y+480),
                        IM_COL32(30, 30, 30, 255));
    ImGui::End();

    // 日志窗口
    ImGui::Begin("Log");
    for (const auto& line : log_lines)
        ImGui::TextUnformatted(line.c_str());
    // 自动滚动到底部
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::End();
}
```

### 24.3 带标签页的设置面板

```cpp
void ShowSettings() {
    ImGui::Begin("Settings");

    if (ImGui::BeginTabBar("SettingsTabs")) {
        // ——— 通用标签 ———
        if (ImGui::BeginTabItem("General")) {
            static bool opt1 = true;
            ImGui::Checkbox("Enable feature A", &opt1);
            static int value = 50;
            ImGui::SliderInt("Quality", &value, 0, 100);
            ImGui::EndTabItem();
        }

        // ——— 显示标签 ———
        if (ImGui::BeginTabItem("Display")) {
            static float bg[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
            ImGui::ColorEdit4("Background", bg);
            ImGui::EndTabItem();
        }

        // ——— 高级标签 ———
        if (ImGui::BeginTabItem("Advanced")) {
            if (ImGui::TreeNode("Expert Options")) {
                static char buf[256] = "";
                ImGui::InputText("Config Path", buf, sizeof(buf));
                ImGui::TreePop();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
```

### 24.4 带右键菜单的列表

```cpp
void ShowItemList(const std::vector<Item>& items) {
    ImGui::Begin("Items");

    for (int i = 0; i < items.size(); i++) {
        ImGui::PushID(i);
        ImGui::Selectable(items[i].name.c_str());

        // 右键菜单
        if (ImGui::BeginPopupContextItem("item_ctx")) {
            if (ImGui::MenuItem("Edit"))    { EditItem(i); }
            if (ImGui::MenuItem("Delete"))  { DeleteItem(i); }
            if (ImGui::MenuItem("Duplicate")) { DuplicateItem(i); }
            ImGui::EndPopup();
        }

        // 拖放支持
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("ITEM", &i, sizeof(int));
            ImGui::Text("Moving %s", items[i].name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (auto* p = ImGui::AcceptDragDropPayload("ITEM")) {
                int src = *(int*)p->Data;
                MoveItem(src, i);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
    }
    ImGui::End();
}
```

---

## 参考链接

- Dear ImGui 官方: https://github.com/ocornut/imgui
- FAQ: https://dearimgui.com/faq
- Wiki: https://github.com/ocornut/imgui/wiki
- Demo 在线版: https://pthom.github.io/imgui_explorer
- 项目头文件: `include/imgui/imgui.h`
