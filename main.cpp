// ============================================================================
//  Mega Man Zero / ZX-inspired game  —  + MENUS, SETTINGS, REBINDABLE KEYS
//  Single-file SDL2 project (beginner-friendly, heavily commented)
//
//  NEW in this version:
//    - Three new screens: Main Menu, Pause Menu, Settings (rebinding).
//    - Input goes through ACTIONS (Jump, Dash, etc.) instead of raw keys, so
//      "the jump key" can be changed at runtime.
//    - Rebinds are saved to controls.cfg next to the executable and loaded
//      on startup — they persist between sessions.
//    - A tiny built-in 5x7 bitmap font: no external font file needed.
//
//  Default controls (changeable in Settings):
//    Left/Right .. move    Z .. jump    X .. dash    C .. saber    V .. buster
//    R .. reset            Esc .. pause / back / quit
//  Menu navigation (NOT rebindable, always these):
//    Up/Down .. move selection    Enter or Z .. confirm    Esc .. back/cancel
// ============================================================================

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <cstdint>

// ----------------------------------------------------------------------------
//  Tunables.
// ----------------------------------------------------------------------------
namespace cfg {
    constexpr int    SCREEN_W = 960, SCREEN_H = 540, TILE = 32;
    constexpr double FIXED_DT = 1.0 / 60.0;

    constexpr double MOVE_SPEED = 220.0, DASH_SPEED = 430.0, DASH_TIME = 0.18;
    constexpr double GRAVITY = 1500.0, JUMP_VEL = -470.0, MAX_FALL = 700.0, JUMP_CUT = 0.45;
    constexpr double WALL_SLIDE_SPEED = 110.0, WALL_JUMP_X = 300.0, WALL_JUMP_Y = -440.0;
    constexpr double COYOTE_TIME = 0.08, JUMP_BUFFER = 0.10;

    constexpr double SABER_ACTIVE = 0.12, SABER_RECOVER = 0.10, SABER_CHAIN = 0.32;
    constexpr int    SABER_DMG[3] = { 3, 3, 5 };
    constexpr double SABER_REACH = 30.0, SABER_TALL = 34.0;
    constexpr double SHOT_SPEED = 520.0, CHARGE_TIME = 0.9, SHOT_LIFETIME = 1.2;
    constexpr int    SHOT_DMG_SMALL = 1, SHOT_DMG_CHARGED = 4;

    constexpr int    DUMMY_HP = 12;
    constexpr double HIT_FLASH_TIME = 0.10;

    constexpr double E_PATROL_SPEED = 70.0, E_CHASE_SPEED = 140.0;
    constexpr double E_SIGHT_RANGE = 220.0, E_ATTACK_RANGE = 40.0;
    constexpr double E_ATTACK_WINDUP = 0.25, E_ATTACK_ACTIVE = 0.18, E_ATTACK_REST = 0.45;
    constexpr double E_GRAVITY = 1500.0;
    constexpr int    E_TOUCH_DMG = 3, E_ATTACK_DMG = 4;

    constexpr int    PLAYER_MAX_HP = 16;
    constexpr double IFRAME_TIME = 0.9;
    constexpr double KNOCKBACK_X = 240.0, KNOCKBACK_Y = -260.0;
}

// ============================================================================
//  TINY BITMAP FONT (5 wide x 7 tall). Avoids needing SDL_ttf + a font file.
//  Each glyph is 7 rows; each row's low 5 bits are the pixels (bit 4 = left).
// ============================================================================
namespace font {
    // Order: A..Z, 0..9, then a few punctuation symbols.
    static const uint8_t GLYPHS[][7] = {
        /*A*/ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
        /*B*/ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        /*C*/ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        /*D*/ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
        /*E*/ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
        /*F*/ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        /*G*/ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
        /*H*/ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        /*I*/ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        /*J*/ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
        /*K*/ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        /*L*/ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        /*M*/ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
        /*N*/ {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
        /*O*/ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        /*P*/ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        /*Q*/ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        /*R*/ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        /*S*/ {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
        /*T*/ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        /*U*/ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        /*V*/ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
        /*W*/ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
        /*X*/ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        /*Y*/ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
        /*Z*/ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
        /*0*/ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
        /*1*/ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        /*2*/ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
        /*3*/ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
        /*4*/ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
        /*5*/ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        /*6*/ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
        /*7*/ {0x1F,0x01,0x02,0x04,0x04,0x04,0x04},
        /*8*/ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        /*9*/ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
        /*space*/ {0,0,0,0,0,0,0},
        /*.*/ {0,0,0,0,0,0x0C,0x0C},
        /*-*/ {0,0,0,0x0E,0,0,0},
        /*:*/ {0,0x0C,0x0C,0,0x0C,0x0C,0},
        /*/*/ {0,0x01,0x02,0x04,0x08,0x10,0},
        /*?*/ {0x0E,0x11,0x01,0x02,0x04,0,0x04},
        /*>*/ {0x10,0x08,0x04,0x02,0x04,0x08,0x10},
        /*!*/ {0x04,0x04,0x04,0x04,0x04,0,0x04},
    };
    enum { IDX_SPACE = 36, IDX_DOT, IDX_DASH, IDX_COLON, IDX_SLASH, IDX_QMARK, IDX_GT, IDX_BANG };

    // Returns the 7-row glyph for c, or the space glyph for unknown chars.
    static const uint8_t* glyph(char c) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') return GLYPHS[c - 'A'];
        if (c >= '0' && c <= '9') return GLYPHS[26 + (c - '0')];
        switch (c) {
            case ' ': return GLYPHS[IDX_SPACE];
            case '.': return GLYPHS[IDX_DOT];
            case '-': return GLYPHS[IDX_DASH];
            case ':': return GLYPHS[IDX_COLON];
            case '/': return GLYPHS[IDX_SLASH];
            case '?': return GLYPHS[IDX_QMARK];
            case '>': return GLYPHS[IDX_GT];
            case '!': return GLYPHS[IDX_BANG];
            default:  return GLYPHS[IDX_SPACE];
        }
    }

    // Draw a string. `s` is the pixel scale (e.g. 2 means each "pixel" is 2x2).
    static void drawText(SDL_Renderer* r, const char* text, int x, int y, int scale,
                         Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca = 255) {
        SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
        int cx = x;
        for (const char* p = text; *p; ++p) {
            const uint8_t* g = glyph(*p);
            for (int row = 0; row < 7; ++row) {
                uint8_t bits = g[row];
                for (int col = 0; col < 5; ++col) {
                    if (bits & (1 << (4 - col))) {
                        SDL_Rect pix{ cx + col * scale, y + row * scale, scale, scale };
                        SDL_RenderFillRect(r, &pix);
                    }
                }
            }
            cx += 6 * scale; // 5 wide + 1 space
        }
    }

    static int textWidth(const char* text, int scale) {
        int n = 0; for (const char* p = text; *p; ++p) ++n;
        return n * 6 * scale - scale; // last char doesn't need trailing space
    }
}

// ============================================================================
//  ACTIONS + BINDINGS + INPUT LAYER
//  This is the indirection that lets keys be rebound. Game code asks
//  "is Jump being pressed?" not "is Z being pressed?".
// ============================================================================
enum Action : int {
    A_LEFT = 0, A_RIGHT, A_JUMP, A_DASH, A_SABER, A_BUSTER, A_RESET, ACTION_COUNT
};
static const char* ACTION_NAME[ACTION_COUNT] = {
    "LEFT", "RIGHT", "JUMP", "DASH", "SABER", "BUSTER", "RESET"
};

struct Bindings { SDL_Scancode keys[ACTION_COUNT]; };

static Bindings defaultBindings() {
    Bindings b{};
    b.keys[A_LEFT]   = SDL_SCANCODE_LEFT;
    b.keys[A_RIGHT]  = SDL_SCANCODE_RIGHT;
    b.keys[A_JUMP]   = SDL_SCANCODE_Z;
    b.keys[A_DASH]   = SDL_SCANCODE_X;
    b.keys[A_SABER]  = SDL_SCANCODE_C;
    b.keys[A_BUSTER] = SDL_SCANCODE_V;
    b.keys[A_RESET]  = SDL_SCANCODE_R;
    return b;
}

// Settings persistence. Plain text:  ACTION=SCANCODE_NUMBER  per line.
static void saveBindings(const Bindings& b) {
    std::ofstream f("controls.cfg");
    if (!f) return;
    for (int i = 0; i < ACTION_COUNT; ++i)
        f << ACTION_NAME[i] << "=" << (int)b.keys[i] << "\n";
}
static void loadBindings(Bindings& b) {
    std::ifstream f("controls.cfg");
    if (!f) return; // no file = keep defaults
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        int code = std::atoi(line.c_str() + eq + 1);
        if (code <= 0 || code >= SDL_NUM_SCANCODES) continue;
        for (int i = 0; i < ACTION_COUNT; ++i)
            if (name == ACTION_NAME[i]) b.keys[i] = (SDL_Scancode)code;
    }
}

struct Input {
    bool held[ACTION_COUNT]    = {};
    bool prev[ACTION_COUNT]    = {};
    bool pressed[ACTION_COUNT] = {}; // edge: true on the frame it first goes down

    void poll(const Uint8* ks, const Bindings& b) {
        for (int i = 0; i < ACTION_COUNT; ++i) {
            prev[i] = held[i];
            held[i] = ks[b.keys[i]] != 0;
            pressed[i] = held[i] && !prev[i];
        }
    }
};

// ============================================================================
//  Game data (unchanged from before).
// ============================================================================
struct Rect {
    double x, y, w, h;
    double right()  const { return x + w; }
    double bottom() const { return y + h; }
    double cx()     const { return x + w * 0.5; }
    double cy()     const { return y + h * 0.5; }
};
static bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() && a.right() > b.x && a.y < b.bottom() && a.bottom() > b.y;
}

struct Level {
    std::vector<std::string> rows;
    int width()  const { return rows.empty() ? 0 : (int)rows[0].size(); }
    int height() const { return (int)rows.size(); }
    bool isSolid(int tx, int ty) const {
        if (ty < 0 || ty >= height() || tx < 0 || tx >= width()) return true;
        return rows[ty][tx] == '#';
    }
};
static Level makeLevel() {
    Level lvl;
    lvl.rows = {
        "##############################",
        "#............................#",
        "#............................#",
        "#......##............##......#",
        "#............................#",
        "#............................#",
        "#...##................##.....#",
        "#............####............#",
        "#............................#",
        "#.....##..............##.....#",
        "#............................#",
        "#..####..........####........#",
        "#............................#",
        "#............................#",
        "#......................###...#",
        "#...###......................#",
        "#............................#",
        "##############################",
    };
    return lvl;
}

struct Player {
    Rect box{ 96, 96, 22, 30 };
    double vx = 0, vy = 0;
    bool onGround = false, onWallLeft = false, onWallRight = false;
    int  facing = 1;
    bool   dashing = false; double dashTimer = 0; bool usedAirDash = false;
    double coyoteTimer = 0, jumpBuffer = 0; bool jumpHeld = false;
    int    saberPhase = 0; double saberTimer = 0; int comboStep = 0;
    double chainTimer = 0; bool saberHasHit = false; bool saberWantNext = false;
    double chargeTime = 0; bool busterHeld = false;
    int    hp = cfg::PLAYER_MAX_HP;
    double iframes = 0; bool alive = true;
};
struct Shot { Rect box; double vx; double life; bool charged; bool alive = true; };

enum class EState { Patrol, Chase, Attack, Hurt };
struct Enemy {
    Rect box{ 600, 96, 30, 46 };
    double vx = 0, vy = 0;
    bool   onGround = false;
    int    facing = -1;
    int    hp = cfg::DUMMY_HP;
    double hitFlash = 0;
    bool   alive = true;
    EState state = EState::Patrol;
    double atkTimer = 0; int atkPhase = 0;
    double hurtTimer = 0;
};

// ----------------------------------------------------------------------------
//  AUDIO (same as before).
// ----------------------------------------------------------------------------
struct Audio {
    Mix_Music* music = nullptr;
    Mix_Chunk *sJump=nullptr,*sDash=nullptr,*sSaber=nullptr,*sShot=nullptr,
              *sCharged=nullptr,*sHit=nullptr,*sHurt=nullptr,*sEnemy=nullptr;
};
static Mix_Chunk* tryLoadWav(const char* path) {
    Mix_Chunk* c = Mix_LoadWAV(path);
    if (!c) SDL_Log("Audio: skip '%s' (%s)", path, Mix_GetError());
    return c;
}
static bool initAudio(Audio& a) {
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        SDL_Log("Mix_OpenAudio: %s", Mix_GetError()); return false;
    }
    Mix_AllocateChannels(16);
    a.music    = Mix_LoadMUS("assets/music.ogg");
    if (!a.music) SDL_Log("Audio: no assets/music.ogg (%s)", Mix_GetError());
    a.sJump    = tryLoadWav("assets/jump.wav");
    a.sDash    = tryLoadWav("assets/dash.wav");
    a.sSaber   = tryLoadWav("assets/saber.wav");
    a.sShot    = tryLoadWav("assets/shot.wav");
    a.sCharged = tryLoadWav("assets/charged.wav");
    a.sHit     = tryLoadWav("assets/hit.wav");
    a.sHurt    = tryLoadWav("assets/hurt.wav");
    a.sEnemy   = tryLoadWav("assets/enemy_down.wav");
    Mix_VolumeMusic(60); Mix_Volume(-1, 96);
    return true;
}
static void shutdownAudio(Audio& a) {
    Mix_HaltMusic();
    if (a.music) Mix_FreeMusic(a.music);
    if (a.sJump) Mix_FreeChunk(a.sJump);
    if (a.sDash) Mix_FreeChunk(a.sDash);
    if (a.sSaber) Mix_FreeChunk(a.sSaber);
    if (a.sShot) Mix_FreeChunk(a.sShot);
    if (a.sCharged) Mix_FreeChunk(a.sCharged);
    if (a.sHit) Mix_FreeChunk(a.sHit);
    if (a.sHurt) Mix_FreeChunk(a.sHurt);
    if (a.sEnemy) Mix_FreeChunk(a.sEnemy);
    Mix_CloseAudio();
}
static void playSfx(Mix_Chunk* c) { if (c) Mix_PlayChannel(-1, c, 0); }

// ----------------------------------------------------------------------------
//  Physics + combat (same as before).
// ----------------------------------------------------------------------------
static void moveAndCollide(Player& p, const Level& lvl, double dt) {
    p.box.x += p.vx * dt;
    p.onWallLeft = p.onWallRight = false;
    {
        int top=(int)std::floor(p.box.y/cfg::TILE), bottom=(int)std::floor((p.box.bottom()-1)/cfg::TILE);
        if (p.vx > 0){ int tx=(int)std::floor((p.box.right()-1)/cfg::TILE);
            for (int ty=top; ty<=bottom; ++ty) if (lvl.isSolid(tx,ty)){ p.box.x=tx*cfg::TILE-p.box.w; p.vx=0; p.onWallRight=true; break; } }
        else if (p.vx < 0){ int tx=(int)std::floor(p.box.x/cfg::TILE);
            for (int ty=top; ty<=bottom; ++ty) if (lvl.isSolid(tx,ty)){ p.box.x=(tx+1)*cfg::TILE; p.vx=0; p.onWallLeft=true; break; } }
    }
    {
        int top=(int)std::floor(p.box.y/cfg::TILE), bottom=(int)std::floor((p.box.bottom()-1)/cfg::TILE);
        int leftTx=(int)std::floor((p.box.x-1)/cfg::TILE), rightTx=(int)std::floor((p.box.right())/cfg::TILE);
        for (int ty=top; ty<=bottom; ++ty){ if (lvl.isSolid(leftTx,ty)) p.onWallLeft=true; if (lvl.isSolid(rightTx,ty)) p.onWallRight=true; }
    }
    p.box.y += p.vy * dt;
    p.onGround = false;
    {
        int left=(int)std::floor(p.box.x/cfg::TILE), right=(int)std::floor((p.box.right()-1)/cfg::TILE);
        if (p.vy > 0){ int ty=(int)std::floor((p.box.bottom()-1)/cfg::TILE);
            for (int tx=left; tx<=right; ++tx) if (lvl.isSolid(tx,ty)){ p.box.y=ty*cfg::TILE-p.box.h; p.vy=0; p.onGround=true; break; } }
        else if (p.vy < 0){ int ty=(int)std::floor(p.box.y/cfg::TILE);
            for (int tx=left; tx<=right; ++tx) if (lvl.isSolid(tx,ty)){ p.box.y=(ty+1)*cfg::TILE; p.vy=0; break; } }
    }
}
static bool enemyMoveCollide(Enemy& e, const Level& lvl, double dt) {
    bool bumped = false;
    e.box.x += e.vx * dt;
    {
        int top=(int)std::floor(e.box.y/cfg::TILE), bottom=(int)std::floor((e.box.bottom()-1)/cfg::TILE);
        if (e.vx > 0){ int tx=(int)std::floor((e.box.right()-1)/cfg::TILE);
            for (int ty=top; ty<=bottom; ++ty) if (lvl.isSolid(tx,ty)){ e.box.x=tx*cfg::TILE-e.box.w; bumped=true; break; } }
        else if (e.vx < 0){ int tx=(int)std::floor(e.box.x/cfg::TILE);
            for (int ty=top; ty<=bottom; ++ty) if (lvl.isSolid(tx,ty)){ e.box.x=(tx+1)*cfg::TILE; bumped=true; break; } }
    }
    e.box.y += e.vy * dt;
    e.onGround = false;
    {
        int left=(int)std::floor(e.box.x/cfg::TILE), right=(int)std::floor((e.box.right()-1)/cfg::TILE);
        if (e.vy > 0){ int ty=(int)std::floor((e.box.bottom()-1)/cfg::TILE);
            for (int tx=left; tx<=right; ++tx) if (lvl.isSolid(tx,ty)){ e.box.y=ty*cfg::TILE-e.box.h; e.vy=0; e.onGround=true; break; } }
        else if (e.vy < 0){ int ty=(int)std::floor(e.box.y/cfg::TILE);
            for (int tx=left; tx<=right; ++tx) if (lvl.isSolid(tx,ty)){ e.box.y=(ty+1)*cfg::TILE; e.vy=0; break; } }
    }
    return bumped;
}
static bool ledgeAhead(const Enemy& e, const Level& lvl, int dir) {
    double footX = (dir > 0) ? e.box.right() + 2 : e.box.x - 2;
    int tx = (int)std::floor(footX / cfg::TILE);
    int ty = (int)std::floor((e.box.bottom() + 2) / cfg::TILE);
    return !lvl.isSolid(tx, ty);
}
static bool getSaberHitbox(const Player& p, Rect& out) {
    if (p.saberPhase != 1) return false;
    double cy = p.box.y + (p.box.h - cfg::SABER_TALL) * 0.5;
    if (p.facing >= 0) out = Rect{ p.box.right(), cy, cfg::SABER_REACH, cfg::SABER_TALL };
    else               out = Rect{ p.box.x - cfg::SABER_REACH, cy, cfg::SABER_REACH, cfg::SABER_TALL };
    return true;
}
static bool getEnemyHitbox(const Enemy& e, Rect& out) {
    if (e.state != EState::Attack || e.atkPhase != 2) return false;
    double reach = 34.0, tall = 30.0;
    double cy = e.box.y + (e.box.h - tall) * 0.5;
    if (e.facing >= 0) out = Rect{ e.box.right(), cy, reach, tall };
    else               out = Rect{ e.box.x - reach, cy, reach, tall };
    return true;
}

static void updatePlayerMovement(Player& p, const Level& lvl, double dt,
                                 bool inLeft, bool inRight, bool jumpPressed, bool dashPressed) {
    if (p.coyoteTimer > 0) p.coyoteTimer -= dt;
    if (p.jumpBuffer  > 0) p.jumpBuffer  -= dt;
    if (jumpPressed) p.jumpBuffer = cfg::JUMP_BUFFER;
    bool wallTouch = (p.onWallLeft || p.onWallRight) && !p.onGround;

    if (dashPressed && !p.dashing && (p.onGround || !p.usedAirDash)) {
        p.dashing = true; p.dashTimer = cfg::DASH_TIME;
        if (!p.onGround) p.usedAirDash = true;
        if (inLeft && !inRight) p.facing = -1;
        if (inRight && !inLeft) p.facing =  1;
    }
    if (p.dashing) { p.dashTimer -= dt; p.vx = cfg::DASH_SPEED * p.facing; if (p.dashTimer <= 0) p.dashing = false; }
    else {
        if (inLeft && !inRight) { p.vx = -cfg::MOVE_SPEED; p.facing = -1; }
        else if (inRight && !inLeft) { p.vx = cfg::MOVE_SPEED; p.facing = 1; }
        else p.vx = 0;
    }
    p.vy += cfg::GRAVITY * dt;
    bool sL = p.onWallLeft && inLeft && p.vy>0, sR = p.onWallRight && inRight && p.vy>0;
    if ((sL||sR) && !p.onGround && p.vy>cfg::WALL_SLIDE_SPEED) p.vy = cfg::WALL_SLIDE_SPEED;
    if (p.vy > cfg::MAX_FALL) p.vy = cfg::MAX_FALL;

    if (p.jumpBuffer > 0) {
        if (p.onGround || p.coyoteTimer > 0) { p.vy=cfg::JUMP_VEL; p.jumpBuffer=0; p.coyoteTimer=0; p.dashing=false; }
        else if (wallTouch) { int away=p.onWallLeft?1:-1; p.vx=cfg::WALL_JUMP_X*away; p.vy=cfg::WALL_JUMP_Y; p.facing=away; p.jumpBuffer=0; p.usedAirDash=false; p.dashing=false; }
    }
    if (!p.jumpHeld && p.vy < 0) p.vy *= cfg::JUMP_CUT;

    moveAndCollide(p, lvl, dt);
    if (p.onGround) { p.coyoteTimer = cfg::COYOTE_TIME; p.usedAirDash = false; }
    if (p.onWallLeft || p.onWallRight) p.usedAirDash = false;
}
static void updateSaber(Player& p, double dt, bool saberPressed) {
    if (p.chainTimer > 0) p.chainTimer -= dt;
    if (saberPressed) {
        if (p.saberPhase == 0) { p.comboStep=0; p.saberPhase=1; p.saberTimer=cfg::SABER_ACTIVE; p.saberHasHit=false; p.chainTimer=cfg::SABER_CHAIN; }
        else p.saberWantNext = true;
    }
    if (p.saberPhase == 1) { p.saberTimer -= dt; if (p.saberTimer<=0){ p.saberPhase=2; p.saberTimer=cfg::SABER_RECOVER; } }
    else if (p.saberPhase == 2) {
        p.saberTimer -= dt;
        if (p.saberTimer <= 0) {
            if (p.saberWantNext && p.chainTimer>0 && p.comboStep<2) { p.comboStep++; p.saberPhase=1; p.saberTimer=cfg::SABER_ACTIVE; p.saberHasHit=false; p.chainTimer=cfg::SABER_CHAIN; }
            else { p.saberPhase=0; p.comboStep=0; }
            p.saberWantNext = false;
        }
    }
}
static void updateBuster(Player& p, double dt, bool busterDown, bool& fired, Shot& out) {
    fired = false;
    if (busterDown) { p.chargeTime += dt; p.busterHeld = true; }
    else if (p.busterHeld) {
        bool charged = p.chargeTime >= cfg::CHARGE_TIME;
        double size = charged ? 18.0 : 8.0;
        double cy = p.box.cy() - size*0.5;
        double sx = (p.facing >= 0) ? p.box.right() : p.box.x - size;
        out.box = Rect{ sx, cy, size, size }; out.vx = cfg::SHOT_SPEED * p.facing;
        out.life = cfg::SHOT_LIFETIME; out.charged = charged; out.alive = true;
        fired = true; p.chargeTime = 0; p.busterHeld = false;
    }
}
static void updateEnemyAI(Enemy& e, const Player& p, const Level& lvl, double dt) {
    if (!e.alive) return;
    if (e.hitFlash > 0) e.hitFlash -= dt;
    e.vy += cfg::E_GRAVITY * dt;
    double dx = p.box.cx() - e.box.cx();
    double dist = std::fabs(dx);
    int dirToPlayer = (dx >= 0) ? 1 : -1;
    if (e.state == EState::Hurt) {
        e.hurtTimer -= dt; e.vx *= 0.85;
        if (e.hurtTimer <= 0) e.state = EState::Patrol;
    } else if (e.state == EState::Attack) {
        e.vx = 0; e.atkTimer -= dt;
        if (e.atkPhase == 1 && e.atkTimer <= 0) { e.atkPhase = 2; e.atkTimer = cfg::E_ATTACK_ACTIVE; }
        else if (e.atkPhase == 2 && e.atkTimer <= 0) { e.atkPhase = 3; e.atkTimer = cfg::E_ATTACK_REST; }
        else if (e.atkPhase == 3 && e.atkTimer <= 0) { e.atkPhase = 0; e.state = (dist < cfg::E_SIGHT_RANGE) ? EState::Chase : EState::Patrol; }
    } else if (e.state == EState::Chase) {
        e.facing = dirToPlayer;
        if (dist <= cfg::E_ATTACK_RANGE) { e.state = EState::Attack; e.atkPhase = 1; e.atkTimer = cfg::E_ATTACK_WINDUP; e.vx = 0; }
        else if (dist > cfg::E_SIGHT_RANGE * 1.3) { e.state = EState::Patrol; }
        else { if (ledgeAhead(e, lvl, e.facing) && e.onGround) e.vx = 0; else e.vx = cfg::E_CHASE_SPEED * e.facing; }
    } else {
        if (dist <= cfg::E_SIGHT_RANGE) e.state = EState::Chase;
        else { e.vx = cfg::E_PATROL_SPEED * e.facing; if (e.onGround && ledgeAhead(e, lvl, e.facing)) e.facing = -e.facing; }
    }
    bool bumped = enemyMoveCollide(e, lvl, dt);
    if (bumped && (e.state == EState::Patrol)) e.facing = -e.facing;
}
static void damageEnemy(Enemy& e, int dmg) {
    if (!e.alive) return;
    e.hp -= dmg; e.hitFlash = cfg::HIT_FLASH_TIME;
    e.state = EState::Hurt; e.hurtTimer = 0.18; e.atkPhase = 0;
    if (e.hp <= 0) { e.hp = 0; e.alive = false; }
}
static void damagePlayer(Player& p, int dmg, int fromDir) {
    if (p.iframes > 0 || !p.alive) return;
    p.hp -= dmg; p.iframes = cfg::IFRAME_TIME;
    p.vx = cfg::KNOCKBACK_X * fromDir; p.vy = cfg::KNOCKBACK_Y;
    p.dashing = false;
    if (p.hp <= 0) { p.hp = 0; p.alive = false; }
}
static void drawRect(SDL_Renderer* r, const Rect& b, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a=255) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, a);
    SDL_Rect d{ (int)std::lround(b.x),(int)std::lround(b.y),(int)std::lround(b.w),(int)std::lround(b.h) };
    SDL_RenderFillRect(r, &d);
}

// ============================================================================
//  SCREENS
// ============================================================================
enum class Screen { MainMenu, Playing, Paused, Settings };

// Centered text helper.
static void drawTextCentered(SDL_Renderer* r, const char* s, int y, int scale,
                             Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca=255) {
    int w = font::textWidth(s, scale);
    font::drawText(r, s, (cfg::SCREEN_W - w)/2, y, scale, cr, cg, cb, ca);
}

// ----------------------------------------------------------------------------
//  Render game world (called for Playing, Paused, and Settings backgrounds).
// ----------------------------------------------------------------------------
static void renderGame(SDL_Renderer* ren, const Level& lvl, const Player& player,
                       const Enemy& enemy, const std::vector<Shot>& shots) {
    for (int ty=0; ty<lvl.height(); ++ty)
        for (int tx=0; tx<lvl.width(); ++tx)
            if (lvl.isSolid(tx,ty))
                drawRect(ren, Rect{(double)tx*cfg::TILE,(double)ty*cfg::TILE,(double)cfg::TILE,(double)cfg::TILE}, 70,80,110);

    if (enemy.alive) {
        Uint8 r=220,g=90,b=90;
        if (enemy.state==EState::Chase)  { r=240; g=150; b=60; }
        if (enemy.state==EState::Attack) { r=255; g=210; b=60; }
        if (enemy.hitFlash>0)            { r=255; g=255; b=255; }
        drawRect(ren, enemy.box, r,g,b);
        if (enemy.state==EState::Attack && enemy.atkPhase==1) {
            double mx = enemy.facing>=0 ? enemy.box.right() : enemy.box.x-6;
            drawRect(ren, Rect{mx, enemy.box.y, 6, enemy.box.h}, 255,255,255,160);
        }
        Rect ehb; if (getEnemyHitbox(enemy, ehb)) drawRect(ren, ehb, 255,120,120,170);
        double frac=(double)enemy.hp/cfg::DUMMY_HP;
        drawRect(ren, Rect{enemy.box.x,enemy.box.y-10,enemy.box.w,5}, 60,60,60);
        drawRect(ren, Rect{enemy.box.x,enemy.box.y-10,enemy.box.w*frac,5}, 90,220,120);
    }
    for (auto& s : shots) drawRect(ren, s.box, s.charged?120:255, s.charged?220:240, s.charged?255:160);

    bool show = !(player.iframes>0 && ((int)(player.iframes*30)%2==0));
    if (player.alive && show) {
        Uint8 r=90,g=200,b=255;
        if (player.dashing) { r=255;g=230;b=90; }
        else if (!player.onGround && (player.onWallLeft||player.onWallRight)) { r=255;g=130;b=200; }
        drawRect(ren, player.box, r,g,b);
    }
    Rect blade; if (getSaberHitbox(player, blade)) drawRect(ren, blade, 230,255,255,180);
    if (player.busterHeld) {
        double frac=player.chargeTime/cfg::CHARGE_TIME; if (frac>1) frac=1;
        Uint8 cr=frac>=1?120:255,cg=frac>=1?220:240,cb=frac>=1?255:160;
        drawRect(ren, Rect{player.box.x,player.box.y-8,player.box.w*frac,4}, cr,cg,cb);
    }
    for (int i=0;i<cfg::PLAYER_MAX_HP;++i) {
        bool filled = i < player.hp;
        drawRect(ren, Rect{12.0+i*14, 12, 11, 16}, filled?90:50, filled?220:50, filled?255:60);
    }
}

// Dim overlay used for pause / settings backgrounds.
static void dimScreen(SDL_Renderer* ren, Uint8 alpha) {
    drawRect(ren, Rect{0,0,(double)cfg::SCREEN_W,(double)cfg::SCREEN_H}, 0,0,0,alpha);
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
    SDL_Window* window = SDL_CreateWindow("MMZ-style: Movement + Combat + AI + Audio + Menus",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cfg::SCREEN_W, cfg::SCREEN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Audio audio; initAudio(audio);
    if (audio.music) Mix_FadeInMusic(audio.music, -1, 800);

    Bindings bindings = defaultBindings();
    loadBindings(bindings); // overrides defaults if controls.cfg exists

    Level lvl = makeLevel();
    Player player; Enemy enemy; std::vector<Shot> shots;

    Screen screen = Screen::MainMenu;
    int  menuIndex   = 0;
    int  rebindIndex = -1;            // -1 = not rebinding; else action being rebound

    // Audio edge-tracking snapshots
    int prevSaberPhase=0, prevComboStep=0, prevPlayerHp=player.hp;
    bool prevEnemyAlive=enemy.alive, prevDashing=false, prevOnGround=true;
    size_t prevShotCount=0; double prevEnemyFlash=0;

    Input input;
    Uint64 prevCounter = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    const double freq = (double)SDL_GetPerformanceFrequency();

    bool running = true;
    while (running) {
        // --- Collect raw key events (needed for rebinding: we want THE NEXT
        // physical key, regardless of any current binding). ---
        SDL_Scancode capturedKey = SDL_SCANCODE_UNKNOWN;
        bool menuUpPressed=false, menuDownPressed=false;
        bool menuConfirmPressed=false, menuBackPressed=false;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                capturedKey = ev.key.keysym.scancode;
                // Hardcoded menu nav keys so menus always work even if bindings break.
                if (capturedKey == SDL_SCANCODE_UP)     menuUpPressed = true;
                if (capturedKey == SDL_SCANCODE_DOWN)   menuDownPressed = true;
                if (capturedKey == SDL_SCANCODE_RETURN || capturedKey == SDL_SCANCODE_KP_ENTER)
                    menuConfirmPressed = true;
                if (capturedKey == SDL_SCANCODE_ESCAPE) menuBackPressed = true;
            }
        }

        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        input.poll(ks, bindings);
        player.jumpHeld = input.held[A_JUMP];

        // --- Rebinding mode swallows the next key press. ---
        if (rebindIndex >= 0 && capturedKey != SDL_SCANCODE_UNKNOWN) {
            if (capturedKey != SDL_SCANCODE_ESCAPE) {
                bindings.keys[rebindIndex] = capturedKey;
                saveBindings(bindings);
            }
            rebindIndex = -1;
            // After rebinding, repoll input so the just-pressed key isn't also
            // interpreted as a confirmation or jump in this same frame.
            input.poll(ks, bindings);
            // Eat menu edges so this same press doesn't navigate the menu.
            menuConfirmPressed = false; menuBackPressed = false;
        }

        // --- Per-screen logic ---
        if (screen == Screen::MainMenu) {
            const int n = 3; // Play, Settings, Quit
            if (menuUpPressed)   menuIndex = (menuIndex - 1 + n) % n;
            if (menuDownPressed) menuIndex = (menuIndex + 1) % n;
            if (menuConfirmPressed) {
                if (menuIndex == 0) { screen = Screen::Playing; }
                else if (menuIndex == 1) { screen = Screen::Settings; menuIndex = 0; }
                else running = false;
            }
            prevCounter = SDL_GetPerformanceCounter(); // freeze sim time
            accumulator = 0;
        }
        else if (screen == Screen::Paused) {
            const int n = 3; // Resume, Settings, Main Menu
            if (menuUpPressed)   menuIndex = (menuIndex - 1 + n) % n;
            if (menuDownPressed) menuIndex = (menuIndex + 1) % n;
            if (menuBackPressed) screen = Screen::Playing;
            if (menuConfirmPressed) {
                if (menuIndex == 0) screen = Screen::Playing;
                else if (menuIndex == 1) { screen = Screen::Settings; menuIndex = 0; }
                else { screen = Screen::MainMenu; menuIndex = 0;
                       player = Player{}; enemy = Enemy{}; shots.clear(); }
            }
            prevCounter = SDL_GetPerformanceCounter();
            accumulator = 0;
        }
        else if (screen == Screen::Settings) {
            const int n = ACTION_COUNT + 1; // each action + a Back item at the end
            if (rebindIndex < 0) {
                if (menuUpPressed)   menuIndex = (menuIndex - 1 + n) % n;
                if (menuDownPressed) menuIndex = (menuIndex + 1) % n;
                if (menuBackPressed) { screen = Screen::MainMenu; menuIndex = 0; }
                if (menuConfirmPressed) {
                    if (menuIndex == ACTION_COUNT) { screen = Screen::MainMenu; menuIndex = 0; }
                    else rebindIndex = menuIndex;
                }
            }
            prevCounter = SDL_GetPerformanceCounter();
            accumulator = 0;
        }
        else if (screen == Screen::Playing) {
            if (menuBackPressed) { screen = Screen::Paused; menuIndex = 0; }

            Uint64 now = SDL_GetPerformanceCounter();
            double frameTime = (now - prevCounter)/freq; prevCounter = now;
            if (frameTime > 0.25) frameTime = 0.25;
            accumulator += frameTime;

            // For the fixed-step loop, edges should only fire on the FIRST step.
            bool jumpPressed   = input.pressed[A_JUMP];
            bool dashPressed   = input.pressed[A_DASH];
            bool saberPressed  = input.pressed[A_SABER];
            bool resetPressed  = input.pressed[A_RESET];
            bool busterDown    = input.held[A_BUSTER];
            bool inLeft        = input.held[A_LEFT];
            bool inRight       = input.held[A_RIGHT];

            if (resetPressed) {
                player = Player{}; enemy = Enemy{}; shots.clear();
                prevSaberPhase=0; prevComboStep=0; prevPlayerHp=player.hp;
                prevEnemyAlive=enemy.alive; prevShotCount=0;
                prevDashing=false; prevOnGround=true; prevEnemyFlash=0;
            }

            while (accumulator >= cfg::FIXED_DT) {
                const double dt = cfg::FIXED_DT;
                if (player.iframes > 0) player.iframes -= dt;
                if (player.alive) {
                    updatePlayerMovement(player, lvl, dt, inLeft, inRight, jumpPressed, dashPressed);
                    updateSaber(player, dt, saberPressed);
                    bool fired=false; Shot ns;
                    updateBuster(player, dt, busterDown, fired, ns);
                    if (fired) shots.push_back(ns);
                }
                updateEnemyAI(enemy, player, lvl, dt);

                Rect blade;
                if (getSaberHitbox(player, blade) && !player.saberHasHit && enemy.alive && overlaps(blade, enemy.box)) {
                    damageEnemy(enemy, cfg::SABER_DMG[player.comboStep]); player.saberHasHit = true;
                }
                for (auto& s : shots) {
                    if (!s.alive) continue;
                    s.box.x += s.vx * dt; s.life -= dt;
                    if (s.life <= 0) { s.alive=false; continue; }
                    int tx=(int)std::floor(s.box.cx()/cfg::TILE), ty=(int)std::floor(s.box.cy()/cfg::TILE);
                    if (lvl.isSolid(tx,ty)) { s.alive=false; continue; }
                    if (enemy.alive && overlaps(s.box, enemy.box)) {
                        damageEnemy(enemy, s.charged?cfg::SHOT_DMG_CHARGED:cfg::SHOT_DMG_SMALL); s.alive=false;
                    }
                }
                shots.erase(std::remove_if(shots.begin(),shots.end(),[](const Shot&s){return !s.alive;}), shots.end());
                Rect ehb;
                if (getEnemyHitbox(enemy, ehb) && player.alive && overlaps(ehb, player.box))
                    damagePlayer(player, cfg::E_ATTACK_DMG, (player.box.cx() < enemy.box.cx()) ? -1 : 1);
                if (enemy.alive && player.alive && overlaps(enemy.box, player.box))
                    damagePlayer(player, cfg::E_TOUCH_DMG, (player.box.cx() < enemy.box.cx()) ? -1 : 1);

                jumpPressed = dashPressed = saberPressed = false;
                accumulator -= cfg::FIXED_DT;
            }

            // Audio edges
            if (prevOnGround && !player.onGround && player.vy < 0) playSfx(audio.sJump);
            if (player.dashing && !prevDashing) playSfx(audio.sDash);
            bool slashStarted = (player.saberPhase == 1 && prevSaberPhase != 1)
                             || (player.saberPhase == 1 && player.comboStep != prevComboStep);
            if (slashStarted) playSfx(audio.sSaber);
            if (shots.size() > prevShotCount) playSfx(shots.back().charged ? audio.sCharged : audio.sShot);
            if (enemy.hitFlash > prevEnemyFlash + 0.001) playSfx(audio.sHit);
            if (prevEnemyAlive && !enemy.alive) playSfx(audio.sEnemy);
            if (player.hp < prevPlayerHp) playSfx(audio.sHurt);
            prevSaberPhase=player.saberPhase; prevComboStep=player.comboStep;
            prevPlayerHp=player.hp; prevEnemyAlive=enemy.alive;
            prevShotCount=shots.size(); prevDashing=player.dashing;
            prevOnGround=player.onGround; prevEnemyFlash=enemy.hitFlash;
        }

        // --- Render ---
        SDL_SetRenderDrawColor(ren, 24,26,38,255); SDL_RenderClear(ren);

        if (screen == Screen::MainMenu) {
            // Title
            drawTextCentered(ren, "Z-Man", 110, 6, 90,220,255);
            drawTextCentered(ren, "Created by Nayeeb Miah", 175, 2, 160,170,200);

            const char* items[3] = { "PLAY", "SETTINGS", "QUIT" };
            for (int i = 0; i < 3; ++i) {
                bool sel = (i == menuIndex);
                int y = 280 + i*45;
                if (sel) drawTextCentered(ren, ">", y, 3, 255,230,90);
                drawTextCentered(ren, items[i], y,
                                 3, sel?255:180, sel?230:180, sel?90:200);
            }
            drawTextCentered(ren, "UP/DOWN: MOVE   ENTER: SELECT", 500, 1, 130,130,150);
        }
        else {
            // Playing / Paused / Settings: draw the game world underneath.
            renderGame(ren, lvl, player, enemy, shots);

            if (screen == Screen::Paused) {
                dimScreen(ren, 160);
                drawTextCentered(ren, "PAUSED", 110, 5, 255,255,255);
                const char* items[3] = { "RESUME", "SETTINGS", "MAIN MENU" };
                for (int i = 0; i < 3; ++i) {
                    bool sel = (i == menuIndex); int y = 230 + i*40;
                    if (sel) drawTextCentered(ren, ">", y, 2, 255,230,90);
                    drawTextCentered(ren, items[i], y, 2,
                                     sel?255:180, sel?230:180, sel?90:200);
                }
            }
            else if (screen == Screen::Settings) {
                dimScreen(ren, 200);
                drawTextCentered(ren, "CONTROLS", 50, 4, 255,255,255);

                int yStart = 130, rowH = 30;
                int colA = cfg::SCREEN_W/2 - 140, colB = cfg::SCREEN_W/2 + 30;
                for (int i = 0; i < ACTION_COUNT; ++i) {
                    bool sel = (i == menuIndex);
                    int y = yStart + i*rowH;
                    Uint8 r = sel?255:180, g = sel?230:180, b = sel?90:200;
                    if (sel) font::drawText(ren, ">", colA - 16, y, 2, 255,230,90);
                    font::drawText(ren, ACTION_NAME[i], colA, y, 2, r,g,b);

                    const char* keyName = SDL_GetScancodeName(bindings.keys[i]);
                    if (!keyName || !*keyName) keyName = "?";
                    if (rebindIndex == i)
                        font::drawText(ren, "PRESS A KEY...", colB, y, 2, 255,230,90);
                    else
                        font::drawText(ren, keyName, colB, y, 2, r,g,b);
                }
                bool backSel = (menuIndex == ACTION_COUNT);
                int yBack = yStart + ACTION_COUNT*rowH + 20;
                if (backSel) drawTextCentered(ren, ">", yBack, 2, 255,230,90);
                drawTextCentered(ren, "BACK", yBack, 2,
                                 backSel?255:180, backSel?230:180, backSel?90:200);

                drawTextCentered(ren, "ENTER: REBIND    ESC: CANCEL/BACK",
                                 cfg::SCREEN_H - 30, 1, 130,130,150);
            }
        }
        SDL_RenderPresent(ren);
    }

    saveBindings(bindings);
    shutdownAudio(audio);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}