#include "pd_api.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static PlaydateAPI* pd = NULL;

static LCDBitmap* numBmp[10] = {0};
static LCDBitmap* frameBmp = NULL;
static LCDBitmap* colonBmp = NULL;
static int cardW = 72;
static int fullH = 96;
static int halfH = 48;

#define DIGIT_COUNT 4
#define FLIP_SPEED 4.0f   // flips per second (~0.25s)
#define DIGIT_SCALE 1.0f

// 每个数字位的翻牌状态
typedef struct DigitFlip {
    char value;        // 当前显示的字符
    char prev;         // 翻牌前的字符
    float progress;    // 0..1，翻牌动画进度
    int flipping;      // 是否正在翻牌
    int dir;           // 翻牌方向：1 = 向下（倒数），-1 = 向上（增加时间）
} DigitFlip;

static DigitFlip digits[DIGIT_COUNT];

typedef enum TimerState {
    TIMER_READY,     // 准备开始，还没运行
    TIMER_RUNNING,   // 正在倒计时
    TIMER_PAUSED,    // 暂停中
    TIMER_FINISHED   // 已结束，显示 00:00
} TimerState;

// Timer 的状态
typedef struct Timer {
    float totalSeconds;   // 总时长，比如 25 * 60
    float secondsLeft;    // 当前剩余秒数
    TimerState state;     // 计时器状态
} Timer;

static Timer timer;

// 上一帧的时间，用来计算 dt
static float lastTime = 0.0f;
static float crankOffset = 0.0f;
static int time = 5;

// 初始化计时器
static void Timer_Init(Timer* t, float seconds)
{
    // TODO:
    // 1. 设置 totalSeconds
    // 2. 设置 secondsLeft
    // 3. 设置 state 为 2
	t->totalSeconds = seconds;
	t->secondsLeft = seconds;
	t->state = TIMER_READY;
}

// 开始 / 暂停切换
static void Timer_Toggle(Timer* t)
{
    // TODO:
    // 如果 state 是 0，就改成 1
    // 如果 state 是 1，就改成 0
	t->state = (t->state == TIMER_RUNNING) ? TIMER_PAUSED : TIMER_RUNNING;
}

// 重置计时器
static void Timer_Reset(Timer* t)
{
    // TODO:
    // 1. secondsLeft 回到 totalSeconds
    // 2. state 改成 2
	t->secondsLeft = t->totalSeconds;
	t->state = TIMER_READY;
}

// 更新计时器
static void Timer_Update(Timer* t, float dt)
{
    // TODO:
    // 如果正在运行：
    //   减少 secondsLeft
    //   如果 secondsLeft <= 0：
    //      secondsLeft = 0
    //      state = 2

    // 注意：
    // 这里先可以用简单做法：
    // 每一帧减 dt 不太方便，因为 secondsLeft 是 int。
    // 你可以先把这个函数留空，下一步我们再处理。

	if (t->state == TIMER_RUNNING) {
		t->secondsLeft -= dt;
		if (t->secondsLeft <= 0) {
			t->secondsLeft = 0;
			t->state = TIMER_FINISHED;	
		}
	}
    
    if (t->state == TIMER_READY) {
        t->secondsLeft = t->totalSeconds;
    }


}

// 把秒数转成 "MM:SS" 字符串
static void FormatTime(float seconds, char* buffer, int bufferSize)
{
    // TODO:
    // 1. minutes = seconds / 60
    // 2. secs = seconds % 60
    // 3. 用 snprintf 写入 buffer
    //
    // 目标格式：
    // 25:00
    // 04:09
    int displaySeconds = (int)ceilf(seconds);
    int minutes = displaySeconds / 60;
    int secs = displaySeconds % 60;
    snprintf(buffer, bufferSize, "%02d:%02d", minutes, secs);
}

// 处理输入
static void HandleInput(void)
{
    PDButtons current;
    PDButtons pushed;
    PDButtons released;

    pd->system->getButtonState(&current, &pushed, &released);
    if (timer.state == TIMER_READY) {
        crankOffset += pd->system->getCrankChange();
        if (fabsf(crankOffset) > 30.0f) {
            if (crankOffset > 0) {
                time++;
            } else {
                time--;
            }

            if (time < 1) {
                time = 1;
            }

            Timer_Init(&timer, time * 30);

            crankOffset -= 30.0f * (crankOffset > 0 ? 1 : -1);
        }
    }
    

    // TODO:
    // 如果刚按下 A，调用 Timer_Toggle(&timer)
    // 如果刚按下 B，调用 Timer_Reset(&timer)
	if (pushed & kButtonA) {
		Timer_Toggle(&timer);
	}

	if (pushed & kButtonB) {
		Timer_Reset(&timer);
	}
}

// 把当前时间写进 digits[]，检测变化时触发翻牌
static void UpdateDigits(float dt)
{
    static float prevSeconds = -1.0f;

    char timeText[16];
    FormatTime(timer.secondsLeft, timeText, sizeof(timeText));

    // timeText 形如 "MM:SS"，取出 4 个数字
    char chars[DIGIT_COUNT] = { timeText[0], timeText[1], timeText[3], timeText[4] };

    // 根据剩余秒数的变化方向决定翻牌方向：
    // 时间增加（上调）-> 向上翻（-1）；否则向下翻（1）
    int dir = (prevSeconds >= 0.0f && timer.secondsLeft > prevSeconds) ? -1 : 1;
    prevSeconds = timer.secondsLeft;

    for (int i = 0; i < DIGIT_COUNT; i++) {
        if (digits[i].value != chars[i]) {
            digits[i].prev = digits[i].value;
            digits[i].value = chars[i];
            digits[i].progress = 0.0f;
            digits[i].flipping = 1;
            digits[i].dir = dir;
        }
        if (digits[i].flipping) {
            digits[i].progress += dt * FLIP_SPEED;
            if (digits[i].progress >= 1.0f) {
                digits[i].progress = 1.0f;
                digits[i].flipping = 0;
            }
        }
    }
}

// 画一个卡片块：先画卡片边框，再画数字，两者共用同样的缩放
// drawY/scaleY 控制折叠，clipY/clipH 限定可见区域
static void DrawCard(int num, int x, float drawY, float s, float scaleY,
                     int clipX, int clipY, int clipW, int clipH)
{
    pd->graphics->setClipRect(clipX, clipY, clipW, clipH);
    pd->graphics->drawScaledBitmap(frameBmp, x, drawY, s, scaleY);
    pd->graphics->drawScaledBitmap(numBmp[num], x, drawY, s, scaleY);
}

// 画一个数字卡片，两段式翻牌动画（边框 + 数字一起折叠）
// dir = 1 向下翻（倒数）；dir = -1 向上翻（增加时间）
static void DrawDigit(int x, int y, char value, char prev, float progress, int dir)
{
    int d = value - '0';
    int p = prev - '0';
    if (d < 0 || d > 9) return;
    if (p < 0 || p > 9) p = d;

    float s = DIGIT_SCALE;
    int cw = (int)(cardW * s);
    int topH = (int)(halfH * s);

    // 透明像素显示为白色：先铺白底
    pd->graphics->fillRect(x, y, cw, topH * 2, kColorWhite);

    if (dir < 0) {
        // 向上翻：底层是旧的上半 + 新的下半
        DrawCard(p, x, y, s, s, x, y, cw, topH);
        DrawCard(d, x, y, s, s, x, y + topH, cw, topH);

        if (progress < 0.5f) {
            // 第一阶段：旧的下半从中线往上折
            float fold = 1.0f - progress / 0.5f;   // 1..0
            int h = (int)(topH * fold);
            int drawY = y + topH - h;
            DrawCard(p, x, drawY, s, s * fold, x, y + topH, cw, h);
        } else {
            // 第二阶段：新的上半从中线往上展开
            float grow = (progress - 0.5f) / 0.5f;  // 0..1
            int h = (int)(topH * grow);
            int drawY = y + topH - h;
            DrawCard(d, x, drawY, s, s * grow, x, y + topH - h, cw, h);
        }
    } else {
        // 向下翻：底层是新的上半 + 旧的下半
        DrawCard(d, x, y, s, s, x, y, cw, topH);
        DrawCard(p, x, y, s, s, x, y + topH, cw, topH);

        if (progress < 0.5f) {
            // 第一阶段：旧的上半从中线往下折
            float fold = 1.0f - progress / 0.5f;   // 1..0
            int h = (int)(topH * fold);
            int drawY = y + topH - h;
            DrawCard(p, x, drawY, s, s * fold, x, drawY, cw, h);
        } else {
            // 第二阶段：新的下半从中线往下展开
            float grow = (progress - 0.5f) / 0.5f;  // 0..1
            int h = (int)(topH * grow);
            int drawY = y + topH - h;
            DrawCard(d, x, drawY, s, s * grow, x, y + topH, cw, h);
        }
    }

    pd->graphics->clearClipRect();
}

// 画 MM:SS 翻牌时钟
static void DrawClock(void)
{
    pd->graphics->setDrawMode(kDrawModeCopy);

    int cw = (int)(cardW * DIGIT_SCALE);
    int ch = (int)(halfH * DIGIT_SCALE) * 2;
    int gap = 8;
    int colonW = 20;
    int totalW = DIGIT_COUNT * cw + 3 * gap + colonW;
    int startX = (400 - totalW) / 2;
    int y = (240 - ch) / 2;

    int x = startX;
    for (int i = 0; i < DIGIT_COUNT; i++) {
        DrawDigit(x, y, digits[i].value, digits[i].prev, digits[i].progress, digits[i].dir);
        x += cw + gap;
        if (i == 1) {
            float cs = (float)ch / 104.0f;
            int colonImgW = (int)(18 * cs);
            x -= gap;
            pd->graphics->drawScaledBitmap(colonBmp, x + (colonW - colonImgW) / 2, y, cs, cs);
            x += colonW;
        }
    }
}

// 绘制界面
static void Draw(void)
{
    pd->graphics->clear(kColorWhite);

    const char* title = "Pocket Focus";
    LCDBitmapDrawMode oldMode = pd->graphics->setDrawMode(kDrawModeFillBlack);

    pd->graphics->drawText(
        title,
        strlen(title),
        kASCIIEncoding,
        20,
        20
    );

    DrawClock();

    const char* statusText;

    // TODO:
    // 如果 timer.state 是 1，statusText = "Running"
    // 否则 statusText = "Paused"
	if (timer.state == TIMER_RUNNING) {
		statusText = "Running";
	} else if (timer.state == TIMER_FINISHED) {
		statusText = "Finished";
	} else if (timer.state == TIMER_READY) {
        statusText = "Ready"; 
    } else {
        statusText = "Paused";  
    }


    // pd->graphics->drawText(
    //     statusText,
    //     strlen(statusText),
    //     kASCIIEncoding,
    //     20,
    //     200
    // );

    const char* hint = "A: Start/Pause   B: Reset";
    pd->graphics->drawText(
        hint,
        strlen(hint),
        kASCIIEncoding,
        20,
        200
    );
    pd->graphics->setDrawMode(oldMode);
}

// 每帧执行
static int update(void* userdata)
{
    (void)userdata;

    float now = pd->system->getElapsedTime();
    float dt = now - lastTime;
    lastTime = now;

    HandleInput();
    Timer_Update(&timer, dt);
    UpdateDigits(dt);
    Draw();

    return 1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg)
{
    (void)arg;

    if (event == kEventInit) {
        pd = playdate;

        pd->display->setRefreshRate(30);

        const char* err = NULL;
        static const char* names[10] = {
            "zero", "one", "two", "three", "four",
            "five", "six", "seven", "eight", "nine"
        };
        for (int i = 0; i < 10; i++) {
            char path[64];
            snprintf(path, sizeof(path), "./images/%s", names[i]);
            numBmp[i] = pd->graphics->loadBitmap(path, &err);
            if (numBmp[i] == NULL) pd->system->logToConsole("%s load failed: %s", names[i], err);
        }
        if (numBmp[0]) {
            pd->graphics->getBitmapData(numBmp[0], &cardW, &fullH, NULL, NULL, NULL);
            halfH = fullH / 2;
        }

        colonBmp = pd->graphics->loadBitmap("./images/colon", &err);
        if (colonBmp == NULL) pd->system->logToConsole("colon load failed: %s", err);

        frameBmp = pd->graphics->loadBitmap("./images/frame", &err);
        if (frameBmp == NULL) pd->system->logToConsole("frame load failed: %s", err);

        for (int i = 0; i < DIGIT_COUNT; i++) {
            digits[i].value = '0';
            digits[i].prev = '0';
            digits[i].progress = 1.0f;
            digits[i].flipping = 0;
            digits[i].dir = 1;
        }

        Timer_Init(&timer, time * 60);

        lastTime = pd->system->getElapsedTime();

        pd->system->setUpdateCallback(update, NULL);
    }

    return 0;
}