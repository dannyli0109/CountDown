# Playdate C API Cheatsheet

面向：用 C 写 Playdate app / 小游戏
适合项目：倒计时器、ToDo、小工具、简单游戏

---

## 0. 最小 C App 结构

```c
#include "pd_api.h" 
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// 保存 Playdate API 指针
// 后面画图、读取按钮、打印日志都通过 pd 调用
static PlaydateAPI* pd = NULL;

// 每帧都会被 Playdate 调用
// 作用：读取输入、更新状态、绘制画面
static int update(void* userdata)
{
    (void)userdata;

    // 清屏
    pd->graphics->clear(kColorWhite);

    // 绘制文本
    const char* text = "Hello Playdate C";

    pd->graphics->drawText(
        text,
        strlen(text),
        kASCIIEncoding,
        20,
        20
    );

    // 返回 1 表示需要刷新屏幕
    return 1;
}

// Windows Simulator 需要导出 eventHandler
#ifdef _WINDLL
__declspec(dllexport)
#endif

// Playdate C app 的入口
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg)
{
    (void)arg;

    if (event == kEventInit) {
        // 保存 API 指针
        pd = playdate;

        // 设置刷新率
        pd->display->setRefreshRate(30);

        // 注册每帧 update 函数
        pd->system->setUpdateCallback(update, NULL);
    }

    return 0;
}
```

---

## 1. App 生命周期

### 入口函数

```c
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg)
```

### 常用事件

```c
kEventInit        // app 初始化
kEventPause       // app 暂停，比如打开系统菜单
kEventResume      // app 恢复
kEventTerminate   // app 退出
kEventLock        // 设备锁定
kEventUnlock      // 设备解锁
kEventLowPower    // 低电量事件
```

### 常用写法

```c
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg)
{
    (void)arg;

    if (event == kEventInit) {
        pd = playdate;

        // 初始化 app 状态
        App_Init();

        // 设置 update loop
        pd->system->setUpdateCallback(update, NULL);
    }

    if (event == kEventPause) {
        // 作用：暂停时可以保存数据
        SaveData();
    }

    if (event == kEventResume) {
        // 作用：从系统菜单回来时恢复状态
    }

    if (event == kEventTerminate) {
        // 作用：退出时保存数据 / 释放资源
        SaveData();
    }

    return 0;
}
```

---

## 2. System：时间、日志、主循环

### 设置 update callback

```c
pd->system->setUpdateCallback(update, NULL);
```

作用：告诉 Playdate 之后每帧调用你的 `update()`。

---

### 打印日志

```c
pd->system->logToConsole("counter = %d", counter);
```

作用：在 Simulator console 里打印调试信息。

---

### 获取运行时间

```c
float now = pd->system->getElapsedTime();
```

作用：获取经过的秒数，常用来算 `dt`。

---

### 重置计时器

```c
pd->system->resetElapsedTime();
```

作用：把 elapsed time 重置为 0。

---

### 常用 Delta Time 写法

```c
static float lastTime = 0.0f;

// 作用：计算这一帧距离上一帧经过了多少秒
static float GetDeltaTime(void)
{
    float now = pd->system->getElapsedTime();
    float dt = now - lastTime;
    lastTime = now;

    return dt;
}
```

---

## 3. Display：屏幕设置

### 设置刷新率

```c
pd->display->setRefreshRate(30);
```

常用：

```c
pd->display->setRefreshRate(30); // 稳定，省电
pd->display->setRefreshRate(50); // 更流畅，但更耗性能
```

---

### 获取屏幕宽高

```c
int screenW = pd->display->getWidth();
int screenH = pd->display->getHeight();
```

Playdate 默认屏幕：

```c
// 通常是：
400 x 240
```

建议不要到处写死 `400` / `240`，而是这样：

```c
int x = pd->display->getWidth() / 2;
int y = pd->display->getHeight() / 2;
```

---

### 反色显示

```c
pd->display->setInverted(1); // 开启反色
pd->display->setInverted(0); // 关闭反色
```

---

### 设置缩放

```c
pd->display->setScale(2);
```

常见 scale：

```c
1
2
4
8
```

---

## 4. Input：按钮输入

### 读取按钮状态

```c
PDButtons current;
PDButtons pushed;
PDButtons released;

pd->system->getButtonState(&current, &pushed, &released);
```

三个状态：

```c
current   // 当前正按着
pushed    // 这一帧刚按下
released  // 这一帧刚松开
```

---

### 按钮常量

```c
kButtonLeft
kButtonRight
kButtonUp
kButtonDown
kButtonA
kButtonB
```

---

### 单次按下

```c
// 作用：A 键刚按下时触发一次
if (pushed & kButtonA) {
    Timer_Toggle(&timer);
}
```

适合：

```text
开始
暂停
确认
重置
打开菜单
```

---

### 持续按住

```c
// 作用：只要按住上键，每帧都会触发
if (current & kButtonUp) {
    value += 1;
}
```

适合：

```text
移动光标
持续调整数值
角色移动
```

---

### 松开触发

```c
// 作用：B 键松开时触发
if (released & kButtonB) {
    CancelAction();
}
```

---

## 5. Input：Crank

### 获取 crank 角度

```c
float angle = pd->system->getCrankAngle();
```

作用：获取 crank 当前角度。

范围大概是：

```text
0 ~ 360
```

---

### 获取 crank 变化量

```c
float change = pd->system->getCrankChange();
```

作用：获取自上次调用以来 crank 转了多少度。

常用在 countdown app：

```c
// 作用：暂停时用 crank 调整时间
static void HandleCrank(Timer* timer)
{
    if (timer->isRunning) {
        return;
    }

    float change = pd->system->getCrankChange();

    if (change > 10.0f) {
        Timer_AddMinute(timer);
    }

    if (change < -10.0f) {
        Timer_RemoveMinute(timer);
    }
}
```

---

### 判断 crank 是否收起

```c
int docked = pd->system->isCrankDocked();
```

```c
if (docked) {
    // crank 收起来了
} else {
    // crank 打开了
}
```

---

## 6. Graphics：颜色

常用颜色：

```c
kColorBlack
kColorWhite
kColorClear
kColorXOR
```

### 清屏

```c
pd->graphics->clear(kColorWhite);
```

---

## 7. Graphics：基础图形

### 画线

```c
pd->graphics->drawLine(
    x1,
    y1,
    x2,
    y2,
    width,
    kColorBlack
);
```

---

### 画矩形

```c
pd->graphics->drawRect(
    x,
    y,
    w,
    h,
    kColorBlack
);
```

---

### 填充矩形

```c
pd->graphics->fillRect(
    x,
    y,
    w,
    h,
    kColorBlack
);
```

---

### 画圆 / 椭圆 / 圆弧

```c
pd->graphics->drawEllipse(
    x,
    y,
    w,
    h,
    lineWidth,
    startAngle,
    endAngle,
    kColorBlack
);
```

例子：画完整圆环

```c
// 作用：画一个圆形边框
pd->graphics->drawEllipse(
    100,
    20,
    200,
    200,
    4,
    0,
    360,
    kColorBlack
);
```

例子：画进度圆弧

```c
// 作用：画从 0 度到 progressAngle 的进度圆弧
pd->graphics->drawEllipse(
    100,
    20,
    200,
    200,
    8,
    0,
    progressAngle,
    kColorBlack
);
```

---

### 填充圆 / 椭圆 / 扇形

```c
pd->graphics->fillEllipse(
    x,
    y,
    w,
    h,
    startAngle,
    endAngle,
    kColorBlack
);
```

---

## 8. Graphics：文字

### 绘制文字

```c
pd->graphics->drawText(
    text,
    strlen(text),
    kASCIIEncoding,
    x,
    y
);
```

常用编码：

```c
kASCIIEncoding
kUTF8Encoding
k16BitLEEncoding
```

---

### 获取文字宽度

```c
int textW = pd->graphics->getTextWidth(
    NULL,
    text,
    strlen(text),
    kASCIIEncoding,
    0
);
```

`NULL` 表示使用当前字体。

---

### 居中画文字

```c
// 作用：把一行文本水平居中绘制
static void DrawTextCentered(const char* text, int y)
{
    int screenW = pd->display->getWidth();

    int textW = pd->graphics->getTextWidth(
        NULL,
        text,
        strlen(text),
        kASCIIEncoding,
        0
    );

    int x = (screenW - textW) / 2;

    pd->graphics->drawText(
        text,
        strlen(text),
        kASCIIEncoding,
        x,
        y
    );
}
```

---

## 9. Font：字体

### 加载字体

```c
const char* err = NULL;

LCDFont* font = pd->graphics->loadFont(
    "fonts/MyFont",
    &err
);
```

### 设置当前字体

```c
if (font == NULL) {
    pd->system->logToConsole("load font failed: %s", err);
} else {
    pd->graphics->setFont(font);
}
```

建议：

```text
字体不要每帧 load。
在 kEventInit 里 load 一次，然后保存成 static 指针。
```

```c
static LCDFont* bigFont = NULL;

// 作用：初始化字体资源
static void LoadFonts(void)
{
    const char* err = NULL;

    bigFont = pd->graphics->loadFont("fonts/big", &err);

    if (bigFont == NULL) {
        pd->system->logToConsole("font load failed: %s", err);
    }
}
```

---

## 10. Bitmap：图片

### 加载图片

```c
const char* err = NULL;

LCDBitmap* image = pd->graphics->loadBitmap(
    "images/icon",
    &err
);
```

### 绘制图片

```c
pd->graphics->drawBitmap(
    image,
    x,
    y,
    kBitmapUnflipped
);
```

### 缩放绘制图片

```c
pd->graphics->drawScaledBitmap(
    image,
    x,
    y,
    2.0f,
    2.0f
);
```

### 释放图片

```c
pd->graphics->freeBitmap(image);
```

---

### 翻转模式

```c
kBitmapUnflipped
kBitmapFlippedX
kBitmapFlippedY
kBitmapFlippedXY
```

---

## 11. File I/O：保存数据

### 写文件

```c
typedef struct SaveData {
    int focusMinutes;
} SaveData;

// 作用：保存倒计时默认分钟数
static void SaveSettings(int minutes)
{
    SaveData data;
    data.focusMinutes = minutes;

    SDFile* file = pd->file->open("settings.dat", kFileWrite);

    if (file == NULL) {
        pd->system->logToConsole("save failed: %s", pd->file->geterr());
        return;
    }

    pd->file->write(file, &data, sizeof(data));
    pd->file->close(file);
}
```

---

### 读文件

```c
// 作用：读取倒计时默认分钟数
static int LoadSettings(void)
{
    SaveData data;
    data.focusMinutes = 25;

    SDFile* file = pd->file->open("settings.dat", kFileReadData);

    if (file == NULL) {
        return data.focusMinutes;
    }

    pd->file->read(file, &data, sizeof(data));
    pd->file->close(file);

    return data.focusMinutes;
}
```

---

### 常用文件模式

```c
kFileReadData  // 从 data folder 读取
kFileWrite     // 写入，覆盖旧文件
kFileAppend    // 追加写入
```

---

## 12. Memory：stack / static / heap

### Stack

适合临时、小、固定大小的数据：

```c
static void Draw(void)
{
    // 作用：临时存储当前帧要显示的时间
    char timeText[32];

    FormatTime(timer.secondsLeft, timeText, sizeof(timeText));
}
```

---

### Static

适合整个 app 生命周期都存在的状态：

```c
static PlaydateAPI* pd = NULL;
static Timer timer;
static float lastTime = 0.0f;
static LCDFont* bigFont = NULL;
```

---

### Heap

适合动态数量、长期存在的数据。

普通 C：

```c
void* memory = malloc(size);

// 使用 memory

free(memory);
```

Playdate 常见：

```c
void* memory = pd->system->realloc(NULL, size);

// 使用 memory

pd->system->realloc(memory, 0);
```

建议：

```text
小 app 里能不用 heap 就先不用。
先用 static fixed array，会更稳。
```

---

## 13. Countdown App 常用代码

### Timer struct

```c
typedef struct Timer {
    float totalSeconds;
    float secondsLeft;
    int isRunning;
} Timer;
```

---

### 初始化

```c
// 作用：初始化倒计时器
static void Timer_Init(Timer* t, float seconds)
{
    t->totalSeconds = seconds;
    t->secondsLeft = seconds;
    t->isRunning = 0;
}
```

---

### 开始 / 暂停

```c
// 作用：切换运行状态
static void Timer_Toggle(Timer* t)
{
    t->isRunning = !t->isRunning;
}
```

---

### 重置

```c
// 作用：重置到当前设置的总时间
static void Timer_Reset(Timer* t)
{
    t->secondsLeft = t->totalSeconds;
    t->isRunning = 0;
}
```

---

### 更新

```c
// 作用：根据 dt 更新倒计时
static void Timer_Update(Timer* t, float dt)
{
    if (!t->isRunning) {
        return;
    }

    t->secondsLeft -= dt;

    if (t->secondsLeft <= 0.0f) {
        t->secondsLeft = 0.0f;
        t->isRunning = 0;
    }
}
```

---

### 格式化时间

```c
#include <math.h>

// 作用：把秒数格式化成 MM:SS
// 注意：倒计时推荐用 ceilf，避免提前跳到下一秒
static void FormatTime(float seconds, char* buffer, int bufferSize)
{
    int displaySeconds = (int)ceilf(seconds);

    int minutes = displaySeconds / 60;
    int secs = displaySeconds % 60;

    snprintf(buffer, bufferSize, "%02d:%02d", minutes, secs);
}
```

如果 `ceilf` 报链接错误，可能需要在 CMake 里链接 math library：

```cmake
target_link_libraries(${PLAYDATE_GAME_NAME} m)
```

---

## 14. 常用 update 结构

```c
// 作用：每帧执行 app 主循环
static int update(void* userdata)
{
    (void)userdata;

    float dt = GetDeltaTime();

    HandleInput();
    HandleCrank(&timer);
    Timer_Update(&timer, dt);

    Draw();

    return 1;
}
```

推荐拆成：

```c
HandleInput();   // 只处理输入
Update(dt);      // 只更新状态
Draw();          // 只负责绘制
```

---

## 15. 常用输入结构

```c
// 作用：处理按钮输入
static void HandleInput(void)
{
    PDButtons current;
    PDButtons pushed;
    PDButtons released;

    pd->system->getButtonState(&current, &pushed, &released);

    if (pushed & kButtonA) {
        Timer_Toggle(&timer);
    }

    if (pushed & kButtonB) {
        Timer_Reset(&timer);
    }

    if (pushed & kButtonUp) {
        Timer_AddMinute(&timer);
    }

    if (pushed & kButtonDown) {
        Timer_RemoveMinute(&timer);
    }

    (void)current;
    (void)released;
}
```

---

## 16. 常用绘制结构

```c
// 作用：绘制整个 app
static void Draw(void)
{
    pd->graphics->clear(kColorWhite);

    DrawTitle();
    DrawTimerRing();
    DrawBigTime();
    DrawHints();
}
```

---

## 17. 大号居中时间

```c
// 作用：在屏幕中间绘制倒计时文本
static void DrawBigTime(const Timer* timer)
{
    char text[32];

    FormatTime(timer->secondsLeft, text, sizeof(text));

    int screenW = pd->display->getWidth();
    int screenH = pd->display->getHeight();

    int textW = pd->graphics->getTextWidth(
        NULL,
        text,
        strlen(text),
        kASCIIEncoding,
        0
    );

    int x = (screenW - textW) / 2;
    int y = (screenH - 24) / 2;

    pd->graphics->drawText(
        text,
        strlen(text),
        kASCIIEncoding,
        x,
        y
    );
}
```

---

## 18. 进度圆环

```c
// 作用：根据剩余时间绘制圆形进度
static void DrawTimerRing(const Timer* timer)
{
    float progress = timer->secondsLeft / timer->totalSeconds;

    if (progress < 0.0f) {
        progress = 0.0f;
    }

    if (progress > 1.0f) {
        progress = 1.0f;
    }

    float endAngle = 360.0f * progress;

    int x = 100;
    int y = 20;
    int size = 200;

    // 底圈
    pd->graphics->drawEllipse(
        x,
        y,
        size,
        size,
        2,
        0,
        360,
        kColorBlack
    );

    // 进度圈
    pd->graphics->drawEllipse(
        x,
        y,
        size,
        size,
        8,
        0,
        endAngle,
        kColorBlack
    );
}
```

---

## 19. 常见 C + Playdate 注意事项

### 不要每帧加载资源

不好：

```c
static int update(void* userdata)
{
    LCDBitmap* img = pd->graphics->loadBitmap("images/icon", NULL);
    pd->graphics->drawBitmap(img, 0, 0, kBitmapUnflipped);
    return 1;
}
```

好：

```c
static LCDBitmap* icon = NULL;

static void LoadAssets(void)
{
    const char* err = NULL;
    icon = pd->graphics->loadBitmap("images/icon", &err);
}
```

---

### 不要返回局部数组

错误：

```c
static char* BadFormatTime(void)
{
    char text[32];
    snprintf(text, sizeof(text), "25:00");
    return text;
}
```

正确：

```c
static void FormatTime(char* buffer, int bufferSize)
{
    snprintf(buffer, bufferSize, "25:00");
}
```

---

### 只在需要时用 heap

优先：

```c
#define MAX_TASKS 32
#define TASK_TEXT_MAX 64

typedef struct Task {
    char text[TASK_TEXT_MAX];
    int done;
} Task;

static Task tasks[MAX_TASKS];
static int taskCount = 0;
```

---

## 20. 最该记住的 10 个 API

```c
pd->system->setUpdateCallback(update, NULL);
pd->system->getButtonState(&current, &pushed, &released);
pd->system->getCrankChange();
pd->system->getCrankAngle();
pd->system->getElapsedTime();
pd->system->logToConsole("value = %d", value);

pd->display->setRefreshRate(30);
pd->display->getWidth();
pd->display->getHeight();

pd->graphics->clear(kColorWhite);
pd->graphics->drawText(text, strlen(text), kASCIIEncoding, x, y);
```

---

## 21. 建议学习顺序

```text
1. eventHandler + update
2. drawText + clear
3. getButtonState
4. Timer struct
5. dt / getElapsedTime
6. crank input
7. drawEllipse / progress ring
8. font loading
9. file save / load
10. 拆 timer.c / timer.h
```

---

## 22. 对 Countdown App 的下一步

当前功能顺序建议：

```text
v0.1  A 开始/暂停，B 重置
v0.2  上下键调整分钟
v0.3  crank 调整分钟
v0.4  大号居中 countdown UI
v0.5  进度圆环
v0.6  保存默认时间
v0.7  focus / break 两种模式
v0.8  完成提示音
```

---

## 23. 核心心智模型

```text
eventHandler()
    ↓
kEventInit
    ↓
保存 PlaydateAPI*
    ↓
setUpdateCallback(update)
    ↓
update() 每帧执行
    ↓
读取输入
    ↓
更新状态
    ↓
绘制画面
```

一句话：

```text
eventHandler 是系统入口，update 是你的 app 主循环。
```
