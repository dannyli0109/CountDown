#include "pd_api.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static PlaydateAPI* pd = NULL;

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
        if (fabsf(crankOffset) > 10.0f) {
            if (crankOffset > 0) {
                time++;
            } else {
                time--;
            }

            if (time < 1) {
                time = 1;
            }

            Timer_Init(&timer, time * 30);

            crankOffset -= 10.0f * (crankOffset > 0 ? 1 : -1);
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

// 绘制界面
static void Draw(void)
{
    pd->graphics->clear(kColorBlack);

    const char* title = "Pocket Focus";
    LCDBitmapDrawMode oldMode = pd->graphics->setDrawMode(kDrawModeFillWhite);

    pd->graphics->drawText(
        title,
        strlen(title),
        kASCIIEncoding,
        20,
        20
    );

    char timeText[32];

    // TODO:
    // 调用 FormatTime，把 timer.secondsLeft 变成文本
	FormatTime(timer.secondsLeft, timeText, sizeof(timeText));

    pd->graphics->drawText(
        timeText,
        strlen(timeText),
        kASCIIEncoding,
        20,
        70
    );

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


    pd->graphics->drawText(
        statusText,
        strlen(statusText),
        kASCIIEncoding,
        20,
        110
    );

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

        Timer_Init(&timer, time * 60);

        lastTime = pd->system->getElapsedTime();

        pd->system->setUpdateCallback(update, NULL);
    }

    return 0;
}