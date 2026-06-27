#include "pd_api.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// ── Screen dimensions ─────────────────────────────────────────────
#define SCREEN_W 400
#define SCREEN_H 240

// ── App limits ────────────────────────────────────────────────────
#define MAX_PLAYDATES  6
#define MAX_PARTICLES  64
#define CELEBRATE_SECS 5.0f

// ── Card geometry ─────────────────────────────────────────────────
#define CARD_X  8
#define CARD_Y  34
#define CARD_W  (SCREEN_W - 16)   // 384
#define CARD_H  168               // leaves room for dots + footer

// ── Types ─────────────────────────────────────────────────────────
typedef enum { STATE_MAIN, STATE_ADD } AppState;

// One upcoming playdate entry
typedef struct {
    char  name[32];
    float origSecs;   // original countdown duration
    float secsLeft;   // seconds remaining
    int   arrived;    // 1 once countdown reaches zero
    float celebTimer; // seconds of celebration animation remaining
} PDEntry;

// A single confetti particle
typedef struct {
    float x, y;   // position
    float vx, vy; // velocity (px/s)
    float life;   // remaining lifetime (seconds)
    int   size;   // 2-4 px
    int   shape;  // 0 = rect, 1 = circle
} Particle;

// State for the "add playdate" flow
typedef struct {
    int   nameIdx;    // index into kNames[]
    int   hours;
    int   minutes;
    float crankAccum; // accumulated crank degrees
} AddFlow;

// ── Globals ───────────────────────────────────────────────────────
static PlaydateAPI* pd        = NULL;
static LCDFont*     clockFont = NULL;
static float        lastTime  = 0.0f;
static AppState     appState  = STATE_MAIN;

static PDEntry entries[MAX_PLAYDATES];
static int     numEntries = 0;
static int     selIdx     = 0;

static Particle particles[MAX_PARTICLES];
static int      numParticles = 0;

static AddFlow addFlow;

// Preset friend names for the add flow
static const char* kNames[10] = {
    "Emma","Liam","Mia","Noah","Ava","Leo","Zoe","Jack","Lily","Owen"
};
#define NUM_NAMES 10

// 8 compass-direction unit vectors used for particle bursts
static const float kVX[8] = { 0.0f, 0.71f, 1.0f, 0.71f, 0.0f,-0.71f,-1.0f,-0.71f};
static const float kVY[8] = {-1.0f,-0.71f, 0.0f, 0.71f, 1.0f, 0.71f, 0.0f,-0.71f};

// ── Particle system ───────────────────────────────────────────────
static void SpawnParticles(float cx, float cy)
{
    numParticles = MAX_PARTICLES;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        float spd       = 45.0f + (float)(i % 6) * 18.0f;
        particles[i].x     = cx;
        particles[i].y     = cy;
        particles[i].vx    = kVX[i % 8] * spd;
        particles[i].vy    = kVY[i % 8] * spd - 20.0f; // slight upward bias
        particles[i].life  = 1.2f + (float)(i % 5) * 0.4f;
        particles[i].size  = 2 + (i % 3);
        particles[i].shape = i % 2;
    }
}

static void UpdateParticles(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life <= 0.0f) continue;
        particles[i].x   += particles[i].vx * dt;
        particles[i].y   += particles[i].vy * dt;
        particles[i].vy  += 140.0f * dt; // gravity
        particles[i].life -= dt;
    }
}

static void DrawParticles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life <= 0.0f) continue;
        int x = (int)particles[i].x;
        int y = (int)particles[i].y;
        int s = particles[i].size;
        if (particles[i].shape == 0)
            pd->graphics->fillRect(x, y, s, s, kColorBlack);
        else
            pd->graphics->fillEllipse(x, y, s, s, 0.0f, 360.0f, kColorBlack);
    }
}

// ── Font helpers ──────────────────────────────────────────────────
static LCDFont* SysFont(void) { return pd->graphics->getSystemFont(); }
static LCDFont* BigFont(void) { return clockFont ? clockFont : pd->graphics->getSystemFont(); }

// ── Text helpers ──────────────────────────────────────────────────
static int TextW(LCDFont* f, const char* s)
{
    return pd->graphics->getTextWidth(f, s, strlen(s), kASCIIEncoding, 0);
}

static void DrawStr(LCDFont* f, const char* s, int x, int y)
{
    pd->graphics->setFont(f);
    pd->graphics->drawText(s, strlen(s), kASCIIEncoding, x, y);
}

// Draw text horizontally centered on screen at the given y
static void DrawStrC(LCDFont* f, const char* s, int y)
{
    DrawStr(f, s, (SCREEN_W - TextW(f, s)) / 2, y);
}

// Safely copy a string into a fixed-size buffer and ensure null-termination
static void SafeStrCopy(char* dest, const char* src, size_t destSize)
{
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}

// ── Decorative helpers ────────────────────────────────────────────

// Draw an 8-pointed star shape using lines
static void DrawStar(int cx, int cy, int r, LCDColor col)
{
    int d = r * 7 / 10;
    pd->graphics->drawLine(cx - r, cy,     cx + r, cy,     1, col);
    pd->graphics->drawLine(cx,     cy - r, cx,     cy + r, 1, col);
    pd->graphics->drawLine(cx - d, cy - d, cx + d, cy + d, 1, col);
    pd->graphics->drawLine(cx + d, cy - d, cx - d, cy + d, 1, col);
}

// ── Time formatting ───────────────────────────────────────────────
static void FormatCountdown(float secs, char* buf, int sz)
{
    int s = (secs < 0.0f) ? 0 : (int)secs;
    int d = s / 86400; s %= 86400;
    int h = s / 3600;  s %= 3600;
    int m = s / 60,  sc = s % 60;
    if (d > 0) snprintf(buf, sz, "%02d:%02d:%02d:%02d", d, h, m, sc);
    else        snprintf(buf, sz, "%02d:%02d:%02d", h, m, sc);
}

// ── Header banner ─────────────────────────────────────────────────
static void DrawHeader(const char* title)
{
    // Black filled banner
    pd->graphics->fillRect(0, 0, SCREEN_W, 28, kColorBlack);
    // White title text via inverted draw mode
    pd->graphics->setDrawMode(kDrawModeInverted);
    DrawStrC(SysFont(), title, 7);
    pd->graphics->setDrawMode(kDrawModeCopy);
    // White star accents at the edges of the banner
    DrawStar(14, 14, 6, kColorWhite);
    DrawStar(SCREEN_W - 14, 14, 6, kColorWhite);
    // Bottom separator line
    pd->graphics->drawLine(0, 29, SCREEN_W, 29, 2, kColorBlack);
}

// ── Selection indicator (dots) ────────────────────────────────────
static void DrawIndicator(int total, int sel)
{
    if (total <= 1) return;
    int r  = 4;
    int sp = r * 2 + 5; // center-to-center spacing
    int tw = total * sp - 5;
    int sx = (SCREEN_W - tw) / 2;
    int y  = SCREEN_H - 22;
    for (int i = 0; i < total; i++) {
        int cx = sx + i * sp + r;
        if (i == sel)
            pd->graphics->fillEllipse(cx - r, y, r * 2, r * 2, 0.0f, 360.0f, kColorBlack);
        else
            pd->graphics->drawEllipse(cx - r, y, r * 2, r * 2, 1, 0.0f, 360.0f, kColorBlack);
    }
}

// ── Hero countdown card ───────────────────────────────────────────
static void DrawCard(PDEntry* e)
{
    int bw = 4; // border thickness

    // Outer thick border (black)
    pd->graphics->fillRect(CARD_X, CARD_Y, CARD_W, CARD_H, kColorBlack);
    // Inner white fill
    pd->graphics->fillRect(CARD_X + bw, CARD_Y + bw,
                           CARD_W - bw * 2, CARD_H - bw * 2, kColorWhite);
    // Inner decorative border line
    pd->graphics->drawRect(CARD_X + bw + 2, CARD_Y + bw + 2,
                           CARD_W - (bw + 2) * 2, CARD_H - (bw + 2) * 2, kColorBlack);

    LCDFont* sys   = SysFont();
    int      sysFH = pd->graphics->getFontHeight(sys);

    // "Playdate with [name]!" label near top of card
    char label[64];
    snprintf(label, sizeof(label), "Playdate with %s!", e->name);
    DrawStrC(sys, label, CARD_Y + 14);

    // Countdown text — try the big clock font, fall back if too wide
    char countBuf[32];
    FormatCountdown(e->secsLeft, countBuf, sizeof(countBuf));

    LCDFont* big   = BigFont();
    int      ctw   = TextW(big, countBuf);
    int      cfh   = pd->graphics->getFontHeight(big);
    int      avail = CARD_W - bw * 2 - 16;

    if (ctw > avail) {
        // Fall back to system font if clock font text is too wide
        big = sys;
        ctw = TextW(sys, countBuf);
        cfh = sysFH;
    }

    // Vertically center the countdown in the space below the name label
    int nameBottom = CARD_Y + 14 + sysFH + 8;
    int cardBottom = CARD_Y + CARD_H - bw - 20; // leave room for bottom star
    int ty         = nameBottom + (cardBottom - nameBottom - cfh) / 2;

    DrawStr(big, countBuf, CARD_X + (CARD_W - ctw) / 2, ty);

    // Decorative star accents inside the card
    DrawStar(CARD_X + 18,             CARD_Y + 18,         7, kColorBlack);
    DrawStar(CARD_X + CARD_W - 18,    CARD_Y + 18,         7, kColorBlack);
    DrawStar(CARD_X + CARD_W / 2,     CARD_Y + CARD_H - 14, 5, kColorBlack);
}

// ── Celebration overlay ───────────────────────────────────────────
static void DrawCelebration(PDEntry* e)
{
    pd->graphics->clear(kColorWhite);
    DrawHeader("Playdate Countdown");

    LCDFont* sys = SysFont();

    // Big celebration messages
    DrawStrC(sys, "It's playdate time!", 72);
    char msg[64];
    snprintf(msg, sizeof(msg), "Time to play with %s!", e->name);
    DrawStrC(sys, msg, 94);

    // Confetti particles drawn on top
    DrawParticles();

    // Large star decorations at the four corners of the content area
    DrawStar(28,            80,            18, kColorBlack);
    DrawStar(SCREEN_W - 28, 80,            18, kColorBlack);
    DrawStar(28,            SCREEN_H - 38, 18, kColorBlack);
    DrawStar(SCREEN_W - 28, SCREEN_H - 38, 18, kColorBlack);

    // Hint at the bottom
    DrawStrC(sys, "Press B to reset", SCREEN_H - 14);
}

// ── Main screen ───────────────────────────────────────────────────
static void DrawMain(void)
{
    pd->graphics->clear(kColorWhite);

    // If the current entry is celebrating, show the full celebration overlay
    if (numEntries > 0) {
        PDEntry* e = &entries[selIdx];
        if (e->arrived && e->celebTimer > 0.0f) {
            DrawCelebration(e);
            return;
        }
    }

    DrawHeader("Playdate Countdown");

    LCDFont* sys = SysFont();

    if (numEntries == 0) {
        DrawStrC(sys, "No playdates yet!", SCREEN_H / 2 - 18);
        DrawStrC(sys, "Press A to add one!", SCREEN_H / 2);
    } else {
        DrawCard(&entries[selIdx]);
        DrawIndicator(numEntries, selIdx);
    }

    // Footer control hints
    const char* footer = (numEntries < MAX_PLAYDATES)
                         ? "L/R:Switch  A:Add  B:Reset"
                         : "L/R:Switch  B:Reset";
    DrawStrC(sys, footer, SCREEN_H - 14);
}

// ── Add-playdate screen ───────────────────────────────────────────
static void DrawAdd(void)
{
    pd->graphics->clear(kColorWhite);
    DrawHeader("Add Playdate");

    LCDFont* sys = SysFont();
    int      fh  = pd->graphics->getFontHeight(sys);

    // Friend name section
    DrawStrC(sys, "Who are you playing with?", 36);

    // Highlighted name picker box
    char nameBuf[40];
    snprintf(nameBuf, sizeof(nameBuf), "<  %s  >", kNames[addFlow.nameIdx]);
    int nw = TextW(sys, nameBuf);
    int nx = (SCREEN_W - nw) / 2;
    pd->graphics->fillRect(nx - 4, 56, nw + 8, fh + 4, kColorBlack);
    pd->graphics->setDrawMode(kDrawModeInverted);
    DrawStr(sys, nameBuf, nx, 58);
    pd->graphics->setDrawMode(kDrawModeCopy);

    // Separator
    pd->graphics->drawLine(20, 82, SCREEN_W - 20, 82, 1, kColorBlack);

    // Duration section
    DrawStrC(sys, "When is the playdate?", 88);

    // Hours and minutes displayed as highlighted black boxes side by side
    char hrBuf[16], minBuf[16];
    snprintf(hrBuf,  sizeof(hrBuf),  "%02d hr%s", addFlow.hours,
             addFlow.hours != 1 ? "s" : "");
    snprintf(minBuf, sizeof(minBuf), "%02d min",  addFlow.minutes);

    int hrW = TextW(sys, hrBuf);
    int mnW = TextW(sys, minBuf);
    int hx  = SCREEN_W / 2 - hrW - 10;
    int mx  = SCREEN_W / 2 + 10;
    int dy  = 108;

    pd->graphics->fillRect(hx - 4, dy - 2, hrW + 8, fh + 4, kColorBlack);
    pd->graphics->setDrawMode(kDrawModeInverted);
    DrawStr(sys, hrBuf, hx, dy);
    pd->graphics->setDrawMode(kDrawModeCopy);

    pd->graphics->fillRect(mx - 4, dy - 2, mnW + 8, fh + 4, kColorBlack);
    pd->graphics->setDrawMode(kDrawModeInverted);
    DrawStr(sys, minBuf, mx, dy);
    pd->graphics->setDrawMode(kDrawModeCopy);

    // Live countdown preview
    float totalSecs = (float)(addFlow.hours * 3600 + addFlow.minutes * 60);
    char  prevBuf[32], previewMsg[48];
    FormatCountdown(totalSecs, prevBuf, sizeof(prevBuf));
    snprintf(previewMsg, sizeof(previewMsg), "Countdown: %s", prevBuf);
    DrawStrC(sys, previewMsg, 138);

    // Instructions
    DrawStrC(sys, "L/R:Name  Up/Dn:+/-30min", 160);
    DrawStrC(sys, "Crank: fine-tune minutes",  176);

    // Footer controls
    DrawStrC(sys, "A: Add Playdate    B: Cancel", SCREEN_H - 14);
}

// ── Input — main state ────────────────────────────────────────────
static void HandleMainInput(PDButtons pushed)
{
    if (numEntries > 0) {
        if (pushed & kButtonLeft) {
            selIdx = (selIdx - 1 + numEntries) % numEntries;
            // Re-spawn confetti if the new entry is currently celebrating
            if (entries[selIdx].arrived && entries[selIdx].celebTimer > 0.0f)
                SpawnParticles(SCREEN_W * 0.5f, SCREEN_H * 0.55f);
            else
                numParticles = 0;
        }
        if (pushed & kButtonRight) {
            selIdx = (selIdx + 1) % numEntries;
            if (entries[selIdx].arrived && entries[selIdx].celebTimer > 0.0f)
                SpawnParticles(SCREEN_W * 0.5f, SCREEN_H * 0.55f);
            else
                numParticles = 0;
        }
        if (pushed & kButtonB) {
            // Reset the selected entry back to its original countdown
            PDEntry* e = &entries[selIdx];
            e->secsLeft   = e->origSecs;
            e->arrived    = 0;
            e->celebTimer = 0.0f;
            numParticles  = 0;
        }
    }
    // A opens the add flow (only if capacity allows)
    if ((pushed & kButtonA) && numEntries < MAX_PLAYDATES) {
        appState           = STATE_ADD;
        addFlow.nameIdx    = 0;
        addFlow.hours      = 1;
        addFlow.minutes    = 30;
        addFlow.crankAccum = 0.0f;
    }
}

// ── Input — add state ─────────────────────────────────────────────
static void HandleAddInput(PDButtons pushed)
{
    // Left/Right: cycle friend name
    if (pushed & kButtonLeft)
        addFlow.nameIdx = (addFlow.nameIdx - 1 + NUM_NAMES) % NUM_NAMES;
    if (pushed & kButtonRight)
        addFlow.nameIdx = (addFlow.nameIdx + 1) % NUM_NAMES;

    // Up/Down: adjust duration by ±30 minutes
    if (pushed & kButtonUp) {
        int tot = addFlow.hours * 60 + addFlow.minutes + 30;
        if (tot > 99 * 60) tot = 99 * 60;
        addFlow.hours   = tot / 60;
        addFlow.minutes = tot % 60;
    }
    if (pushed & kButtonDown) {
        int tot = addFlow.hours * 60 + addFlow.minutes - 30;
        if (tot < 1) tot = 1; // minimum 1 minute
        addFlow.hours   = tot / 60;
        addFlow.minutes = tot % 60;
    }

    // Crank: ±1 minute per 6 degrees of rotation
    float crankChange = pd->system->getCrankChange();
    addFlow.crankAccum += crankChange;
    while (addFlow.crankAccum >= 6.0f) {
        addFlow.crankAccum -= 6.0f;
        int tot = addFlow.hours * 60 + addFlow.minutes + 1;
        if (tot > 99 * 60) tot = 99 * 60;
        addFlow.hours   = tot / 60;
        addFlow.minutes = tot % 60;
    }
    while (addFlow.crankAccum <= -6.0f) {
        addFlow.crankAccum += 6.0f;
        int tot = addFlow.hours * 60 + addFlow.minutes - 1;
        if (tot < 1) tot = 1;
        addFlow.hours   = tot / 60;
        addFlow.minutes = tot % 60;
    }

    // A: confirm and add the new entry
    if (pushed & kButtonA) {
        if (numEntries < MAX_PLAYDATES) {
            PDEntry* e = &entries[numEntries];
            SafeStrCopy(e->name, kNames[addFlow.nameIdx], sizeof(e->name));
            float secs = (float)(addFlow.hours * 3600 + addFlow.minutes * 60);
            if (secs <= 0.0f) secs = 60.0f;
            e->origSecs   = secs;
            e->secsLeft   = secs;
            e->arrived    = 0;
            e->celebTimer = 0.0f;
            selIdx = numEntries;
            numEntries++;
        }
        appState = STATE_MAIN;
    }
    // B: cancel without adding
    if (pushed & kButtonB)
        appState = STATE_MAIN;
}

// ── Input dispatcher ──────────────────────────────────────────────
static void HandleInput(void)
{
    PDButtons cur, pushed, released;
    pd->system->getButtonState(&cur, &pushed, &released);
    (void)cur;
    (void)released;

    if      (appState == STATE_MAIN) HandleMainInput(pushed);
    else if (appState == STATE_ADD)  HandleAddInput(pushed);
}

// ── Update entries ────────────────────────────────────────────────
static void UpdateEntries(float dt)
{
    for (int i = 0; i < numEntries; i++) {
        if (entries[i].arrived) {
            // Tick down the celebration animation timer
            if (entries[i].celebTimer > 0.0f) {
                entries[i].celebTimer -= dt;
                if (entries[i].celebTimer < 0.0f) entries[i].celebTimer = 0.0f;
            }
            continue;
        }
        entries[i].secsLeft -= dt;
        if (entries[i].secsLeft <= 0.0f) {
            entries[i].secsLeft   = 0.0f;
            entries[i].arrived    = 1;
            entries[i].celebTimer = CELEBRATE_SECS;
            // Spawn confetti for the selected entry (or any if no particles active)
            if (i == selIdx || numParticles == 0)
                SpawnParticles(SCREEN_W * 0.5f, SCREEN_H * 0.55f);
        }
    }
}

// ── Per-frame update callback ─────────────────────────────────────
static int update(void* userdata)
{
    (void)userdata;

    float now = pd->system->getElapsedTime();
    float dt  = now - lastTime;
    lastTime  = now;
    // Cap dt to 100 ms: prevents particles and timers jumping forward on the
    // first frame (where getElapsedTime() may return a large initial value)
    // or after the app is paused/suspended by the system.
    if (dt > 0.1f) dt = 0.1f;

    HandleInput();

    if (appState == STATE_MAIN) {
        UpdateEntries(dt);
        UpdateParticles(dt);
        DrawMain();
    } else {
        DrawAdd();
    }

    return 1;
}

// ── Event handler ─────────────────────────────────────────────────
#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg)
{
    (void)arg;

    if (event == kEventInit) {
        pd = playdate;
        pd->display->setRefreshRate(30);

        // Attempt to load the Mikodacs Clock font (path relative to bundle root)
        // Falls back to NULL which BigFont() handles gracefully
        const char* fontErr = NULL;
        clockFont = pd->graphics->loadFont("fonts/Mikodacs-Clock", &fontErr);

        // Pre-seed 3 example playdates so the app looks alive on first launch
        SafeStrCopy(entries[0].name, "Emma", sizeof(entries[0].name));
        entries[0].origSecs   = 2.0f * 60.0f;       // 2 minutes (quick to see countdown)
        entries[0].secsLeft   = entries[0].origSecs;
        entries[0].arrived    = 0;
        entries[0].celebTimer = 0.0f;

        SafeStrCopy(entries[1].name, "Liam", sizeof(entries[1].name));
        entries[1].origSecs   = 1.5f * 3600.0f;     // 1 hour 30 minutes
        entries[1].secsLeft   = entries[1].origSecs;
        entries[1].arrived    = 0;
        entries[1].celebTimer = 0.0f;

        SafeStrCopy(entries[2].name, "Mia", sizeof(entries[2].name));
        entries[2].origSecs   = 24.0f * 3600.0f;    // 1 day
        entries[2].secsLeft   = entries[2].origSecs;
        entries[2].arrived    = 0;
        entries[2].celebTimer = 0.0f;

        numEntries = 3;
        selIdx     = 0;

        lastTime = pd->system->getElapsedTime();
        pd->system->setUpdateCallback(update, NULL);
    }

    return 0;
}