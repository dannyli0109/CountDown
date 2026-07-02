#include "pd_api.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static PlaydateAPI* pd = NULL;

static LCDBitmap* numBmp[10] = {0};
static LCDBitmap* frameBmp = NULL;
static LCDBitmap* colonBmp = NULL;
static LCDFont* uiFont = NULL;
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

// 常用时长预设（分钟），上/下方向键切换
static const int timePresets[] = { 1, 3, 5, 10, 15, 25, 45, 60 };
#define PRESET_COUNT (int)(sizeof(timePresets) / sizeof(timePresets[0]))

// 暗色模式（反相显示）
static int darkMode = 0;
static PDMenuItem* darkModeItem = NULL;

// 进度条样式：0=水波条，1=整屏水位下降，2=圆环，3=下雪
enum BarStyle { BAR_WATER = 0, BAR_FLOOD = 1, BAR_RING = 2, BAR_SNOW = 3 };
static int barStyle = BAR_WATER;
static PDMenuItem* barStyleItem = NULL;

// 下雪样式：黑夜空 + 白雪花飘落 + 底部积雪堆积（结束时铺满全屏）
#define SNOW_COUNT 200
typedef struct SnowFlake {
    float x, y;       // 位置
    float speed;      // 下落速度 (px/s)
    float drift;      // 水平漂移相位
    float driftAmp;   // 漂移幅度
    int size;         // 2 或 3 px
} SnowFlake;
static SnowFlake snow[SNOW_COUNT];
static int snowInited = 0;

// 当前数字缩放（圆环样式下缩小以便被大圆环包住）
static float digitScale = DIGIT_SCALE;

// 结束提醒：闪屏 + 提示音
#define FLASH_DURATION 2.0f
#define FLASH_RATE 6.0f
#define CHIME_GAP 0.13f
static float flashTimer = 0.0f;
static int chimeStep = 0;
static float chimeTimer = 0.0f;
static PDSynth* finishSynth = NULL;
static const float chimeNotes[3] = { 523.25f, 659.25f, 783.99f }; // C5 E5 G5

// 暂停指示器：闪烁计时
#define PAUSE_BLINK_PERIOD 1.0f   // 一个完整闪烁周期（秒）
static float pauseBlink = 0.0f;

// 保存设置（时长 + 暗色模式 + 进度条样式）到数据目录
static void SaveSettings(void)
{
    SDFile* f = pd->file->open("settings.dat", kFileWrite);
    if (f) {
        int32_t data[3] = { time, darkMode, barStyle };
        pd->file->write(f, data, sizeof(data));
        pd->file->close(f);
    }
}

// 读取上次保存的设置
static void LoadSettings(void)
{
    SDFile* f = pd->file->open("settings.dat", kFileReadData);
    if (f) {
        int32_t data[3] = { 0, 0, 0 };
        int n = pd->file->read(f, data, sizeof(data));
        if (n >= (int)(2 * sizeof(int32_t))) {
            if (data[0] >= 1) time = data[0];
            darkMode = data[1] ? 1 : 0;
        }
        if (n >= (int)(3 * sizeof(int32_t))) {
            if (data[2] >= 0 && data[2] <= 3) barStyle = data[2];
        }
        pd->file->close(f);
    }
}

// 菜单回调：切换暗色模式
static void DarkModeCallback(void* userdata)
{
    (void)userdata;
    darkMode = pd->system->getMenuItemValue(darkModeItem);
    pd->display->setInverted(darkMode);
    SaveSettings();
}

// 菜单回调：切换进度条样式
static void BarStyleCallback(void* userdata)
{
    (void)userdata;
    barStyle = pd->system->getMenuItemValue(barStyleItem);
    SaveSettings();
}

// 计时结束：触发闪屏 + 播放提示音
static void TriggerFinish(void)
{
    flashTimer = FLASH_DURATION;
    chimeTimer = 0.0f;
    chimeStep = 0;
    if (finishSynth) {
        pd->sound->synth->playNote(finishSynth, chimeNotes[0], 1.0f, 0.12f, 0);
        chimeStep = 1;
    }
}

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
        // 上/下方向键在预设时长之间切换（无环绕）
        if (pushed & (kButtonUp | kButtonDown)) {
            // 找到当前 time 对应或最接近的预设下标
            int idx = 0;
            for (int i = 0; i < PRESET_COUNT; i++) {
                if (timePresets[i] <= time) idx = i;
            }
            if (pushed & kButtonUp) {
                if (idx < PRESET_COUNT - 1) idx++;
            } else {
                if (idx > 0) idx--;
            }
            time = timePresets[idx];
            Timer_Init(&timer, time * 60);
            crankOffset = 0.0f;
            SaveSettings();
        }

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
            if (time > 99) {
                time = 99;
            }

            Timer_Init(&timer, time * 60);

            crankOffset -= 30.0f * (crankOffset > 0 ? 1 : -1);
            SaveSettings();
        }
    } else if (timer.state == TIMER_FINISHED) {
        // 结束后转动曲柄唤醒，回到 READY 状态（保留上次的时长）
        if (pd->system->getCrankChange() != 0.0f) {
            timer.state = TIMER_READY;
            crankOffset = 0.0f;
        }
    }
    

    // TODO:
    // 如果刚按下 A，调用 Timer_Toggle(&timer)
    // 如果刚按下 B，调用 Timer_Reset(&timer)
    // A：结束时回到 READY；否则开始/暂停切换
    if (pushed & kButtonA) {
        if (timer.state == TIMER_FINISHED) {
            timer.state = TIMER_READY;
            crankOffset = 0.0f;
        } else {
            Timer_Toggle(&timer);
        }
    }

    // B：仅在暂停或结束时才重置
    if (pushed & kButtonB) {
        if (timer.state == TIMER_PAUSED || timer.state == TIMER_FINISHED) {
            Timer_Reset(&timer);
        }
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

    float s = digitScale;
    int cw = (int)(cardW * s);
    int topH = (int)(halfH * s);

    // 透明像素显示为白色：先铺白底
    // Flood / Snow 样式下跳过白底，让边框的透明圆角露出背后的背景
    if (barStyle != BAR_FLOOD && barStyle != BAR_SNOW) {
        pd->graphics->fillRect(x, y, cw, topH * 2, kColorWhite);
    }

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

// 画带白色描边的冒号：先在 8 个方向画白色轮廓，再画正常黑色冒号
// 这样在深色背景（Flood/Snow）上也能看清
static void DrawColon(int x, int y, float cs)
{
    // 白色描边（8 个 1px 偏移）
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            pd->graphics->drawScaledBitmap(colonBmp, x + dx, y + dy, cs, cs);
        }
    }
    // 正常黑色冒号
    pd->graphics->setDrawMode(kDrawModeCopy);
    pd->graphics->drawScaledBitmap(colonBmp, x, y, cs, cs);
}

// 画 MM:SS 翻牌时钟
static void DrawClock(void)
{
    pd->graphics->setDrawMode(kDrawModeCopy);

    // 圆环样式：两行 2x2 布局（上行分钟，下行秒），数字更大
    if (barStyle == BAR_RING) {
        // 分钟为 0（不足 1 分钟）：只显示放大居中的秒数
        if (digits[0].value == '0' && digits[1].value == '0') {
            digitScale = 1.1f;
            int cw = (int)(cardW * digitScale);
            int ch = (int)(halfH * digitScale) * 2;
            int colGap = (int)(8 * digitScale);
            int rowW = 2 * cw + colGap;
            int startX = (400 - rowW) / 2;
            int rowY = (240 - ch) / 2;   // 以圆环中心垂直居中

            DrawDigit(startX, rowY, digits[2].value, digits[2].prev, digits[2].progress, digits[2].dir);
            DrawDigit(startX + cw + colGap, rowY, digits[3].value, digits[3].prev, digits[3].progress, digits[3].dir);
            return;
        }

        digitScale = 0.82f;   // 让 2x2 卡片四角都落在圆环内
        int cw = (int)(cardW * digitScale);
        int ch = (int)(halfH * digitScale) * 2;
        int colGap = (int)(8 * digitScale);
        int rowGap = (int)(6 * digitScale);
        int rowW = 2 * cw + colGap;
        int startX = (400 - rowW) / 2;
        int totalH = 2 * ch + rowGap;
        int topY = (240 - totalH) / 2;   // 以圆环中心 (120) 垂直居中
        int botY = topY + ch + rowGap;

        // 上行：分钟两位（digits[0], digits[1]）
        DrawDigit(startX, topY, digits[0].value, digits[0].prev, digits[0].progress, digits[0].dir);
        DrawDigit(startX + cw + colGap, topY, digits[1].value, digits[1].prev, digits[1].progress, digits[1].dir);
        // 下行：秒两位（digits[2], digits[3]）
        DrawDigit(startX, botY, digits[2].value, digits[2].prev, digits[2].progress, digits[2].dir);
        DrawDigit(startX + cw + colGap, botY, digits[3].value, digits[3].prev, digits[3].progress, digits[3].dir);
        return;
    }

    digitScale = DIGIT_SCALE;

    int cw = (int)(cardW * digitScale);
    int ch = (int)(halfH * digitScale) * 2;
    int gap = (int)(8 * digitScale);
    int colonW = (int)(20 * digitScale);
    int totalW = DIGIT_COUNT * cw + 2 * gap + colonW;
    int startX = (400 - totalW) / 2;
    // 水波条样式：时钟 + 进度条整体垂直居中；其它样式时钟单独居中
    int y;
    if (barStyle == BAR_WATER) {
        int barH = 20;
        int groupGap = 16;
        int groupH = ch + groupGap + barH;
        y = (240 - groupH) / 2;
    } else {
        y = (240 - ch) / 2;
    }

    int x = startX;
    for (int i = 0; i < DIGIT_COUNT; i++) {
        DrawDigit(x, y, digits[i].value, digits[i].prev, digits[i].progress, digits[i].dir);
        x += cw + gap;
        if (i == 1) {
            // 冒号带白色描边，在任意背景（含 Flood/Snow）上都可见
            float cs = (float)ch / 104.0f;
            int colonImgW = (int)(18 * cs);
            DrawColon(x - gap + (colonW - colonImgW) / 2, y, cs);
            x += colonW - gap;
        }
    }
}

// 画倒计时进度条（剩余时间，左对齐，向右递减）
// 运行时右侧边缘像水面一样上下起伏（逐行正弦偏移），营造动态感
static void DrawWaterBar(void)
{
    pd->graphics->setDrawMode(kDrawModeCopy);

    // 与时钟一起垂直居中：groupH = 时钟高 + 间距 + 条高
    int ch = (int)(halfH * DIGIT_SCALE) * 2;
    int bh = 20;
    int groupGap = 16;
    int groupH = ch + groupGap + bh;
    int groupTop = (240 - groupH) / 2;

    int bx = 40;
    int by = groupTop + ch + groupGap;
    int bw = 320;

    float fraction = timer.totalSeconds > 0.0f
        ? timer.secondsLeft / timer.totalSeconds
        : 0.0f;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    // 轨道边框（总长度始终可见）
    pd->graphics->drawRect(bx, by, bw, bh, kColorBlack);

    float baseW = bw * fraction;

    // 只有运行时才起伏；振幅随进度条流速变化（越快浪越大）
    float waveAmp = 0.0f;
    if (timer.state == TIMER_RUNNING) {
        // 流速 = 每秒排空的像素数（线性排空，短计时更快）
        float speed = timer.totalSeconds > 0.0f ? bw / timer.totalSeconds : 0.0f;
        waveAmp = speed * 3.0f;
        if (waveAmp < 1.0f) waveAmp = 1.0f;
        if (waveAmp > 3.0f) waveAmp = 3.0f;
    }

    // 两个不同频率/速度的正弦叠加，波峰漂移且不会很快重复，显得更自然
    const float k1 = 6.2831853f / 20.0f;  // 主涌浪：沿高度一个波长
    const float w1 = 2.0f;
    const float k2 = 6.2831853f / 7.0f;   // 次涟漪：更短更快
    const float w2 = 2.0f;

    // 逐行绘制，每行边缘随两波叠加偏移
    for (int row = 0; row < bh; row++) {
        float edge = baseW;
        if (waveAmp > 0.0f) {
            float wave = 0.6f * sinf(k1 * row + w1 * lastTime)
                       + 0.4f * sinf(k2 * row + w2 * lastTime);
            edge += waveAmp * wave;
        }
        if (edge < 0.0f) edge = 0.0f;
        if (edge > bw) edge = (float)bw;

        int w = (int)edge;
        if (w > 0) {
            pd->graphics->fillRect(bx, by + row, w, 1, kColorBlack);
            // 水面处画一条黑白交错的泡沫带，像翻涌的浪花
            if (waveAmp > 0.0f) {
                int t = (int)(lastTime * 6.0f);
                // 最外沿 4 格做棋盘式黑白交错，形成更厚的滚动泡沫带
                for (int col = 0; col < 4; col++) {
                    int px = w - 1 - col;
                    if (px < 0) break;
                    if (((t + row + col) & 1) == 0) {
                        pd->graphics->fillRect(bx + px, by + row, 1, 1, kColorWhite);
                    }
                }
            }
        }
    }
}

// 计算当前剩余比例 [0,1]
static float RemainingFraction(void)
{
    float fraction = timer.totalSeconds > 0.0f
        ? timer.secondsLeft / timer.totalSeconds
        : 0.0f;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    return fraction;
}

// 整屏水位样式：黑色水面从上往下退去（剩余越多黑色越满），水面带波纹泡沫
static void DrawFloodBackground(void)
{
    pd->graphics->setDrawMode(kDrawModeCopy);

    float fraction = RemainingFraction();

    // 水面 y：剩余满时在顶部（整屏黑），耗尽时在底部
    float surfaceY = 240.0f * (1.0f - fraction);

    // 运行时的波动幅度（随流速变化）
    float waveAmp = 0.0f;
    if (timer.state == TIMER_RUNNING) {
        float speed = timer.totalSeconds > 0.0f ? 240.0f / timer.totalSeconds : 0.0f;
        waveAmp = speed * 3.0f;
        if (waveAmp < 1.0f) waveAmp = 1.0f;
        if (waveAmp > 4.0f) waveAmp = 4.0f;
    }

    const float k1 = 6.2831853f / 90.0f;   // 沿屏宽的主涌浪
    const float w1 = 2.0f;
    const float k2 = 6.2831853f / 33.0f;   // 次涟漪
    const float w2 = 2.0f;

    // 逐列绘制，黑色填在水面以下
    for (int col = 0; col < 400; col++) {
        float top = surfaceY;
        if (waveAmp > 0.0f) {
            float wave = 0.6f * sinf(k1 * col + w1 * lastTime)
                       + 0.4f * sinf(k2 * col + w2 * lastTime);
            top += waveAmp * wave;
        }
        if (top < 0.0f) top = 0.0f;
        if (top > 240.0f) top = 240.0f;

        int y0 = (int)top;
        if (y0 < 240) {
            pd->graphics->fillRect(col, y0, 1, 240 - y0, kColorBlack);
            // 水面处黑白交错泡沫带
            if (waveAmp > 0.0f) {
                int t = (int)(lastTime * 6.0f);
                for (int r = 0; r < 4; r++) {
                    int py = y0 + r;
                    if (py >= 240) break;
                    if (((t + col + r) & 1) == 0) {
                        pd->graphics->fillRect(col, py, 1, 1, kColorWhite);
                    }
                }
            }
        }
    }
}

// 圆环样式：整屏居中的进度圆环，剩余部分从顶端顺时针填充
static void DrawRing(void)
{
    pd->graphics->setDrawMode(kDrawModeCopy);

    float fraction = RemainingFraction();

    int diameter = 236;
    int rx = (400 - diameter) / 2;
    int ry = (240 - diameter) / 2;

    // 底圈轨道（细线，始终可见）
    pd->graphics->drawEllipse(rx, ry, diameter, diameter, 4, 0.0f, 360.0f, kColorBlack);

    // 剩余弧（粗线，从正上方顺时针）
    if (fraction > 0.0f) {
        float endAngle = 360.0f * fraction;
        pd->graphics->drawEllipse(rx, ry, diameter, diameter, 10, 0.0f, endAngle, kColorBlack);
    }
}

static float SnowRand(void)
{
    return (float)rand() / (float)RAND_MAX;
}

// 初始化雪花池（随机位置/速度/漂移）
static void InitSnow(void)
{
    for (int i = 0; i < SNOW_COUNT; i++) {
        snow[i].x = SnowRand() * 400.0f;
        snow[i].y = SnowRand() * 240.0f;
        snow[i].speed = 20.0f + SnowRand() * 40.0f;   // 20..60 px/s
        snow[i].drift = SnowRand() * 6.2831853f;
        snow[i].driftAmp = 4.0f + SnowRand() * 8.0f;  // 4..12 px
        snow[i].size = (SnowRand() < 0.5f) ? 2 : 3;
    }
    snowInited = 1;
}

// 当前活跃雪花数：剩余越多雪越密，随剩余时间减少而变稀（天空在收缩）
static int SnowActiveCount(void)
{
    int n = (int)(SNOW_COUNT * RemainingFraction());
    if (n < 6) n = 6;                 // 保留最少量，不会完全空
    if (n > SNOW_COUNT) n = SNOW_COUNT;
    return n;
}

// 更新雪花：仅在计时运行时飘落，落到雪面以下则从顶部重生
static void UpdateSnow(float dt)
{
    if (!snowInited) InitSnow();
    if (timer.state != TIMER_RUNNING) return;

    int active = SnowActiveCount();
    float surfaceY = 240.0f * RemainingFraction();   // 雪面 y
    for (int i = 0; i < active; i++) {
        snow[i].y += snow[i].speed * dt;
        snow[i].drift += dt * 1.5f;
        if (snow[i].y >= surfaceY) {
            snow[i].y = -2.0f;
            snow[i].x = SnowRand() * 400.0f;
            snow[i].speed = 20.0f + SnowRand() * 40.0f;
        }
    }
}

// 下雪样式：黑夜空 + 底部白雪堆积（剩余越少雪越高）+ 白雪花飘落
static void DrawSnowBackground(void)
{
    pd->graphics->setDrawMode(kDrawModeCopy);

    // 黑色夜空铺满
    pd->graphics->fillRect(0, 0, 400, 240, kColorBlack);

    // 底部积雪：白色从底往上堆积，雪面带轻微波纹
    float surfaceY = 240.0f * RemainingFraction();
    const float k1 = 6.2831853f / 70.0f;
    const float k2 = 6.2831853f / 27.0f;
    for (int col = 0; col < 400; col++) {
        float wave = 0.6f * sinf(k1 * col) + 0.4f * sinf(k2 * col + lastTime * 0.5f);
        float top = surfaceY + 3.0f * wave;
        if (top < 0.0f) top = 0.0f;
        if (top > 240.0f) top = 240.0f;
        int y0 = (int)top;
        if (y0 < 240) {
            pd->graphics->fillRect(col, y0, 1, 240 - y0, kColorWhite);
        }
    }

    // 飘落的白雪花（只画在雪面以上），数量随剩余时间减少
    if (!snowInited) InitSnow();
    int active = SnowActiveCount();
    for (int i = 0; i < active; i++) {
        int py = (int)snow[i].y;
        if (py < 0) continue;
        int px = (int)(snow[i].x + snow[i].driftAmp * sinf(snow[i].drift));
        if (px < 0) px += 400;
        if (px >= 400) px -= 400;
        pd->graphics->fillRect(px, py, snow[i].size, snow[i].size, kColorWhite);
    }
}

// 根据当前样式绘制进度指示
static void DrawProgressBar(void)
{
    if (barStyle == BAR_WATER) {
        DrawWaterBar();
    } else if (barStyle == BAR_RING) {
        DrawRing();
    }
    // BAR_FLOOD 由背景绘制（在时钟之前），此处不处理
}

// 画带白色描边的文字，在任意背景上都可见
static void DrawTextOutlined(const char* text, int x, int y)
{
    int len = (int)strlen(text);
    // 白色描边（8 个 1px 偏移）
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            pd->graphics->drawText(text, len, kASCIIEncoding, x + dx, y + dy);
        }
    }
    // 正常黑色文字
    pd->graphics->setDrawMode(kDrawModeFillBlack);
    pd->graphics->drawText(text, len, kASCIIEncoding, x, y);
}

// READY 状态：在右侧竖排预览所有时长预设，当前项高亮
static void DrawPresetList(void)
{
    if (!uiFont) return;
    pd->graphics->setFont(uiFont);

    int fontH = pd->graphics->getFontHeight(uiFont);
    int rowH = fontH + 8;
    int totalH = PRESET_COUNT * rowH;
    int startY = (240 - totalH) / 2;
    int colCenter = 378;   // 数字列的水平中心

    for (int i = 0; i < PRESET_COUNT; i++) {
        // 反序显示：最大值在顶部，最小值在底部
        int preset = timePresets[PRESET_COUNT - 1 - i];
        char label[8];
        snprintf(label, sizeof(label), "%d", preset);
        int tw = pd->graphics->getTextWidth(uiFont, label, strlen(label), kASCIIEncoding, 0);
        int ty = startY + i * rowH + (rowH - fontH) / 2;
        int tx = colCenter - tw / 2;   // 居中对齐

        if (preset == time) {
            // 选中项：贴合文字的白底圆角框 + 黑字
            int pad = 5;
            int boxW = tw + pad * 2;
            int boxLeft = colCenter - boxW / 2;
            pd->graphics->fillRect(boxLeft, startY + i * rowH + 1, boxW, rowH - 2, kColorWhite);
            pd->graphics->drawRect(boxLeft, startY + i * rowH + 1, boxW, rowH - 2, kColorBlack);
            pd->graphics->setDrawMode(kDrawModeFillBlack);
            pd->graphics->drawText(label, strlen(label), kASCIIEncoding, tx, ty);
        } else {
            DrawTextOutlined(label, tx, ty);
        }
    }

    pd->graphics->setDrawMode(kDrawModeCopy);
}

// 绘制界面
static void Draw(void)
{
    pd->graphics->clear(kColorWhite);

    if (barStyle == BAR_FLOOD) {
        DrawFloodBackground();
    } else if (barStyle == BAR_SNOW) {
        DrawSnowBackground();
    }

    // 暂停时：时钟做干净的开/关闪烁（前 60% 显示、后 40% 隐藏）
    int hideClock = 0;
    if (timer.state == TIMER_PAUSED) {
        float phase = pauseBlink / PAUSE_BLINK_PERIOD;
        if (phase >= 0.6f) hideClock = 1;
    }

    if (!hideClock) {
        DrawClock();
    }

    DrawProgressBar();

    if (timer.state == TIMER_READY) {
        DrawPresetList();
    }
}

// 每帧执行
static int update(void* userdata)
{
    (void)userdata;

    static TimerState prevState = TIMER_READY;

    float now = pd->system->getElapsedTime();
    float dt = now - lastTime;
    lastTime = now;

    HandleInput();
    Timer_Update(&timer, dt);

    if (barStyle == BAR_SNOW) {
        UpdateSnow(dt);
    }

    // 暂停闪烁计时（仅在暂停时推进，其余状态归零）
    if (timer.state == TIMER_PAUSED) {
        pauseBlink += dt;
        if (pauseBlink >= PAUSE_BLINK_PERIOD) pauseBlink -= PAUSE_BLINK_PERIOD;
    } else {
        pauseBlink = 0.0f;
    }

    // 进入 FINISHED 的瞬间：触发结束提醒
    if (timer.state == TIMER_FINISHED && prevState != TIMER_FINISHED) {
        TriggerFinish();
    }
    // 离开 FINISHED（例如曲柄唤醒）：立即停止闪屏
    if (prevState == TIMER_FINISHED && timer.state != TIMER_FINISHED) {
        flashTimer = 0.0f;
        pd->display->setInverted(darkMode);
    }
    prevState = timer.state;

    // 提示音音序（三音上行）
    if (chimeStep > 0 && chimeStep < 3) {
        chimeTimer += dt;
        if (chimeTimer >= CHIME_GAP) {
            chimeTimer -= CHIME_GAP;
            if (finishSynth) {
                pd->sound->synth->playNote(finishSynth, chimeNotes[chimeStep], 1.0f, 0.12f, 0);
            }
            chimeStep++;
        }
    }

    // 闪屏：在正常/反相之间快速切换
    if (flashTimer > 0.0f) {
        flashTimer -= dt;
        int on = ((int)(flashTimer * FLASH_RATE)) & 1;
        pd->display->setInverted(on ? !darkMode : darkMode);
        if (flashTimer <= 0.0f) {
            flashTimer = 0.0f;
            pd->display->setInverted(darkMode);
        }
    }

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

        // UI 字体（用于预设列表）
        uiFont = pd->graphics->loadFont("fonts/AshevilleSans14Bold", &err);
        if (uiFont == NULL) pd->system->logToConsole("ui font load failed: %s", err);

        // 读取上次保存的时长与暗色模式
        LoadSettings();

        // 结束提示音合成器
        finishSynth = pd->sound->synth->newSynth();
        if (finishSynth) {
            pd->sound->synth->setWaveform(finishSynth, kWaveformSine);
        }

        for (int i = 0; i < DIGIT_COUNT; i++) {
            digits[i].value = '0';
            digits[i].prev = '0';
            digits[i].progress = 1.0f;
            digits[i].flipping = 0;
            digits[i].dir = 1;
        }

        Timer_Init(&timer, time * 60);

        lastTime = pd->system->getElapsedTime();

        darkModeItem = pd->system->addCheckmarkMenuItem(
            "Dark Mode", darkMode, DarkModeCallback, NULL);
        pd->display->setInverted(darkMode);

        static const char* barStyleOptions[4] = { "Water", "Flood", "Ring", "Snow" };
        barStyleItem = pd->system->addOptionsMenuItem(
            "Bar Style", barStyleOptions, 4, BarStyleCallback, NULL);
        pd->system->setMenuItemValue(barStyleItem, barStyle);

        pd->system->setUpdateCallback(update, NULL);
    }

    return 0;
}