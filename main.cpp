// ============================================================================
//  Mega Man Zero / ZX-inspired game  —  movement + combat + ENEMY AI
//  Single-file SDL2 project (beginner-friendly, heavily commented)
//
//  NEW in this version (enemy AI):
//    - A proper enemy with its own physics (gravity + ground/wall collision).
//    - A 4-state AI brain: PATROL -> CHASE -> ATTACK -> HURT, using simple
//      sensing (how far is the player? is the player in front of me?).
//    - Ledge detection so it won't walk off platforms while patrolling.
//    - Contact damage to the player, with invincibility frames (i-frames) and
//      knockback so taking a hit feels fair, not punishing.
//    - The player can now die; press R to reset the whole encounter.
//
//  Controls:
//    Left/Right .. move    Z(hold) .. jump    X .. dash/air-dash
//    C .. saber (tap x3 = combo)   V .. buster (tap = shot, hold = charge)
//    R .. reset encounter          Escape .. quit
// ============================================================================

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace cfg {
    constexpr int    SCREEN_W = 960, SCREEN_H = 540, TILE = 32;
    constexpr double FIXED_DT = 1.0 / 60.0;

    constexpr double MOVE_SPEED = 220.0, DASH_SPEED = 430.0, DASH_TIME = 0.18;
    constexpr double GRAVITY = 1500.0, JUMP_VEL = -470.0, MAX_FALL = 700.0, JUMP_CUT = 0.45;
    constexpr double WALL_SLIDE_SPEED = 110.0, WALL_JUMP_X = 300.0, WALL_JUMP_Y = -440.0;
    constexpr double COYOTE_TIME = 0.08, JUMP_BUFFER = 0.10;

    // Combat
    constexpr double SABER_ACTIVE = 0.12, SABER_RECOVER = 0.10, SABER_CHAIN = 0.32;
    constexpr int    SABER_DMG[3] = { 3, 3, 5 };
    constexpr double SABER_REACH = 30.0, SABER_TALL = 34.0;
    constexpr double SHOT_SPEED = 520.0, CHARGE_TIME = 0.9, SHOT_LIFETIME = 1.2;
    constexpr int    SHOT_DMG_SMALL = 1, SHOT_DMG_CHARGED = 4;

    constexpr int    DUMMY_HP = 12;
    constexpr double HIT_FLASH_TIME = 0.10;

    // ---- ENEMY AI tuning ----
    constexpr double E_PATROL_SPEED = 70.0;   // walking speed while patrolling
    constexpr double E_CHASE_SPEED  = 140.0;  // faster when chasing the player
    constexpr double E_SIGHT_RANGE  = 220.0;  // distance at which it notices you
    constexpr double E_ATTACK_RANGE = 40.0;   // distance at which it attacks
    constexpr double E_ATTACK_WINDUP= 0.25;   // telegraph before the hit lands
    constexpr double E_ATTACK_ACTIVE= 0.18;   // how long its strike is dangerous
    constexpr double E_ATTACK_REST  = 0.45;   // cooldown after attacking
    constexpr double E_GRAVITY      = 1500.0;
    constexpr int    E_TOUCH_DMG    = 3;      // contact / attack damage to player
    constexpr int    E_ATTACK_DMG   = 4;

    // ---- PLAYER survivability ----
    constexpr int    PLAYER_MAX_HP  = 16;
    constexpr double IFRAME_TIME    = 0.9;    // invincibility after a hit (sec)
    constexpr double KNOCKBACK_X    = 240.0;  // pushed back when hit
    constexpr double KNOCKBACK_Y    = -260.0;
}

// ----------------------------------------------------------------------------
//  AUDIO. SDL_mixer splits sound into two kinds:
//    - Mix_Music: one streamed background track at a time (MP3/OGG).
//    - Mix_Chunk: short sound effects, played on channels (WAV recommended).
//  Put your files in an "assets/" folder next to the executable. Anything
//  that fails to load will be left as nullptr and silently skipped — so the
//  game still runs even if a file is missing or named differently.
// ----------------------------------------------------------------------------
struct Audio {
    Mix_Music* music    = nullptr; // background track
    Mix_Chunk* sJump    = nullptr;
    Mix_Chunk* sDash    = nullptr;
    Mix_Chunk* sSaber   = nullptr;
    Mix_Chunk* sShot    = nullptr;
    Mix_Chunk* sCharged = nullptr;
    Mix_Chunk* sHit     = nullptr; // saber/shot connects on enemy
    Mix_Chunk* sHurt    = nullptr; // player takes damage
    Mix_Chunk* sEnemy   = nullptr; // enemy defeated
};

// Tries to load a chunk; returns nullptr (and logs) if it fails.
static Mix_Chunk* tryLoadWav(const char* path) {
    Mix_Chunk* c = Mix_LoadWAV(path);
    if (!c) SDL_Log("Audio: could not load '%s' (%s) — skipping.", path, Mix_GetError());
    return c;
}

static bool initAudio(Audio& a) {
    // 44100 Hz, default format, stereo, 1024-byte buffer.
    // Smaller buffer = less latency, more CPU. 1024 is a good default.
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(16); // up to 16 overlapping SFX

    // Load each file. Missing files are fine — they just won't play.
    a.music    = Mix_LoadMUS("assets/music.ogg");
    if (!a.music) SDL_Log("Audio: no assets/music.ogg (%s) — running silent.", Mix_GetError());

    a.sJump    = tryLoadWav("assets/jump.wav");
    a.sDash    = tryLoadWav("assets/dash.wav");
    a.sSaber   = tryLoadWav("assets/saber.wav");
    a.sShot    = tryLoadWav("assets/shot.wav");
    a.sCharged = tryLoadWav("assets/charged.wav");
    a.sHit     = tryLoadWav("assets/hit.wav");
    a.sHurt    = tryLoadWav("assets/hurt.wav");
    a.sEnemy   = tryLoadWav("assets/enemy_down.wav");

    // Set sensible default volumes (0..128). Music quieter than SFX.
    Mix_VolumeMusic(60);
    Mix_Volume(-1, 96); // -1 = all channels

    return true;
}

static void shutdownAudio(Audio& a) {
    Mix_HaltMusic();
    if (a.music)    Mix_FreeMusic(a.music);
    if (a.sJump)    Mix_FreeChunk(a.sJump);
    if (a.sDash)    Mix_FreeChunk(a.sDash);
    if (a.sSaber)   Mix_FreeChunk(a.sSaber);
    if (a.sShot)    Mix_FreeChunk(a.sShot);
    if (a.sCharged) Mix_FreeChunk(a.sCharged);
    if (a.sHit)     Mix_FreeChunk(a.sHit);
    if (a.sHurt)    Mix_FreeChunk(a.sHurt);
    if (a.sEnemy)   Mix_FreeChunk(a.sEnemy);
    Mix_CloseAudio();
}

// Safe play helper: ignores null chunks so missing files don't crash anything.
static void playSfx(Mix_Chunk* c) {
    if (c) Mix_PlayChannel(-1, c, 0); // -1 = first free channel; 0 = no loops
}

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
    double iframes = 0;   // >0 = currently invincible & flashing
    bool   alive = true;
};

struct Shot { Rect box; double vx; double life; bool charged; bool alive = true; };

// ----------------------------------------------------------------------------
//  Enemy with an AI state machine.
//  States:
//    PATROL  - walk back and forth, turn at walls/ledges, watch for player
//    CHASE   - player spotted within sight; move toward them
//    ATTACK  - close enough to strike: windup -> active(dangerous) -> rest
//    HURT    - briefly stunned after taking a hit
// ----------------------------------------------------------------------------
enum class EState { Patrol, Chase, Attack, Hurt };

struct Enemy {
    Rect box{ 600, 96, 30, 46 };
    double vx = 0, vy = 0;
    bool   onGround = false;
    int    facing = -1;               // start facing left
    int    hp = cfg::DUMMY_HP;
    double hitFlash = 0;
    bool   alive = true;

    EState state = EState::Patrol;
    double atkTimer = 0;              // times the windup/active/rest phases
    int    atkPhase = 0;             // 0=none 1=windup 2=active 3=rest
    double hurtTimer = 0;
};

// ----------------------------------------------------------------------------
//  Player tile collision (unchanged).
// ----------------------------------------------------------------------------
static void moveAndCollide(Player& p, const Level& lvl, double dt) {
    p.box.x += p.vx * dt;
    p.onWallLeft = p.onWallRight = false;
    {
        int top = (int)std::floor(p.box.y/cfg::TILE), bottom=(int)std::floor((p.box.bottom()-1)/cfg::TILE);
        if (p.vx > 0) { int tx=(int)std::floor((p.box.right()-1)/cfg::TILE);
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

// A simpler collide routine for the enemy (it only walks & falls).
// Returns true if it bumped a wall horizontally (used to turn around).
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

// Would the enemy step off a ledge if it keeps walking in `dir`?
// We probe the tile just ahead of and below the enemy's leading foot.
static bool ledgeAhead(const Enemy& e, const Level& lvl, int dir) {
    double footX = (dir > 0) ? e.box.right() + 2 : e.box.x - 2;
    int tx = (int)std::floor(footX / cfg::TILE);
    int ty = (int)std::floor((e.box.bottom() + 2) / cfg::TILE);
    return !lvl.isSolid(tx, ty); // no floor ahead => it's a ledge
}

static bool getSaberHitbox(const Player& p, Rect& out) {
    if (p.saberPhase != 1) return false;
    double cy = p.box.y + (p.box.h - cfg::SABER_TALL) * 0.5;
    if (p.facing >= 0) out = Rect{ p.box.right(), cy, cfg::SABER_REACH, cfg::SABER_TALL };
    else               out = Rect{ p.box.x - cfg::SABER_REACH, cy, cfg::SABER_REACH, cfg::SABER_TALL };
    return true;
}

// Enemy's strike hitbox, live only during its ATTACK active phase.
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

// ----------------------------------------------------------------------------
//  THE AI BRAIN. Runs once per fixed step. Reads the player position, decides
//  a state, and sets the enemy's velocity / attack timers accordingly.
// ----------------------------------------------------------------------------
static void updateEnemyAI(Enemy& e, const Player& p, const Level& lvl, double dt) {
    if (!e.alive) return;

    if (e.hitFlash > 0) e.hitFlash -= dt;
    e.vy += cfg::E_GRAVITY * dt;       // gravity always applies

    // How far is the player, and on which side?
    double dx = p.box.cx() - e.box.cx();
    double dist = std::fabs(dx);
    int dirToPlayer = (dx >= 0) ? 1 : -1;

    // --- HURT state: briefly stunned, no movement input ---
    if (e.state == EState::Hurt) {
        e.hurtTimer -= dt;
        e.vx *= 0.85; // friction during stun
        if (e.hurtTimer <= 0) e.state = EState::Patrol;
    }
    // --- ATTACK state: windup -> active -> rest, then back to chase/patrol ---
    else if (e.state == EState::Attack) {
        e.vx = 0; // plant feet to strike
        e.atkTimer -= dt;
        if (e.atkPhase == 1 && e.atkTimer <= 0) { e.atkPhase = 2; e.atkTimer = cfg::E_ATTACK_ACTIVE; }
        else if (e.atkPhase == 2 && e.atkTimer <= 0) { e.atkPhase = 3; e.atkTimer = cfg::E_ATTACK_REST; }
        else if (e.atkPhase == 3 && e.atkTimer <= 0) {
            e.atkPhase = 0;
            e.state = (dist < cfg::E_SIGHT_RANGE) ? EState::Chase : EState::Patrol;
        }
    }
    // --- CHASE state: move toward the player; attack when close ---
    else if (e.state == EState::Chase) {
        e.facing = dirToPlayer;
        if (dist <= cfg::E_ATTACK_RANGE) {
            e.state = EState::Attack; e.atkPhase = 1; e.atkTimer = cfg::E_ATTACK_WINDUP; e.vx = 0;
        } else if (dist > cfg::E_SIGHT_RANGE * 1.3) {
            e.state = EState::Patrol;   // lost sight (with a little hysteresis)
        } else {
            // Don't chase off a ledge — stop at the edge instead of falling.
            if (ledgeAhead(e, lvl, e.facing) && e.onGround) e.vx = 0;
            else e.vx = cfg::E_CHASE_SPEED * e.facing;
        }
    }
    // --- PATROL state: walk, turn at walls/ledges, watch for the player ---
    else {
        if (dist <= cfg::E_SIGHT_RANGE) { e.state = EState::Chase; }
        else {
            e.vx = cfg::E_PATROL_SPEED * e.facing;
            if (e.onGround && ledgeAhead(e, lvl, e.facing)) e.facing = -e.facing; // turn at ledge
        }
    }

    bool bumped = enemyMoveCollide(e, lvl, dt);
    if (bumped && (e.state == EState::Patrol)) e.facing = -e.facing; // turn at wall
}

static void damageEnemy(Enemy& e, int dmg) {
    if (!e.alive) return;
    e.hp -= dmg; e.hitFlash = cfg::HIT_FLASH_TIME;
    e.state = EState::Hurt; e.hurtTimer = 0.18; e.atkPhase = 0;
    if (e.hp <= 0) { e.hp = 0; e.alive = false; }
}

// Hurt the player: only lands if not currently in i-frames. Applies knockback.
static void damagePlayer(Player& p, int dmg, int fromDir) {
    if (p.iframes > 0 || !p.alive) return;
    p.hp -= dmg;
    p.iframes = cfg::IFRAME_TIME;
    p.vx = cfg::KNOCKBACK_X * fromDir;
    p.vy = cfg::KNOCKBACK_Y;
    p.dashing = false;
    if (p.hp <= 0) { p.hp = 0; p.alive = false; }
}

static void drawRect(SDL_Renderer* r, const Rect& b, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a=255) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, a);
    SDL_Rect d{ (int)std::lround(b.x),(int)std::lround(b.y),(int)std::lround(b.w),(int)std::lround(b.h) };
    SDL_RenderFillRect(r, &d);
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
    SDL_Window* window = SDL_CreateWindow("MMZ-style: Movement + Combat + AI + Audio",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cfg::SCREEN_W, cfg::SCREEN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    // Bring up audio. If this fails the game still runs, just silently.
    Audio audio;
    initAudio(audio);
    if (audio.music) Mix_FadeInMusic(audio.music, -1, 800); // -1 loops forever, fade in over 800ms

    Level lvl = makeLevel();
    Player player;
    Enemy  enemy;
    std::vector<Shot> shots;

    // Track previous-frame snapshots so we can detect EVENTS (edges) rather
    // than just states. A sound should fire when something HAPPENS, not every
    // frame while that thing is true.
    int    prevSaberPhase = 0;
    int    prevComboStep  = 0;
    int    prevPlayerHp   = player.hp;
    bool   prevEnemyAlive = enemy.alive;
    size_t prevShotCount  = 0;
    bool   prevDashing    = false;
    bool   prevOnGround   = true;

    bool pJump=false,pDash=false,pSaber=false,pReset=false;
    Uint64 prevCounter = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    const double freq = (double)SDL_GetPerformanceFrequency();

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        bool inLeft=ks[SDL_SCANCODE_LEFT], inRight=ks[SDL_SCANCODE_RIGHT];
        bool jumpNow=ks[SDL_SCANCODE_Z], dashNow=ks[SDL_SCANCODE_X];
        bool saberNow=ks[SDL_SCANCODE_C], busterNow=ks[SDL_SCANCODE_V], resetNow=ks[SDL_SCANCODE_R];

        bool jumpPressed=jumpNow&&!pJump, dashPressed=dashNow&&!pDash, saberPressed=saberNow&&!pSaber, resetPressed=resetNow&&!pReset;
        pJump=jumpNow; pDash=dashNow; pSaber=saberNow; pReset=resetNow;
        player.jumpHeld = jumpNow;

        if (resetPressed) {
            player = Player{}; enemy = Enemy{}; shots.clear();
            prevSaberPhase = 0; prevComboStep = 0;
            prevPlayerHp = player.hp; prevEnemyAlive = enemy.alive;
            prevShotCount = 0; prevDashing = false; prevOnGround = true;
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double frameTime = (now - prevCounter)/freq; prevCounter = now;
        if (frameTime > 0.25) frameTime = 0.25;
        accumulator += frameTime;

        while (accumulator >= cfg::FIXED_DT) {
            const double dt = cfg::FIXED_DT;

            if (player.iframes > 0) player.iframes -= dt;

            if (player.alive) {
                updatePlayerMovement(player, lvl, dt, inLeft, inRight, jumpPressed, dashPressed);
                updateSaber(player, dt, saberPressed);
                bool fired=false; Shot ns;
                updateBuster(player, dt, busterNow, fired, ns);
                if (fired) shots.push_back(ns);
            }

            updateEnemyAI(enemy, player, lvl, dt);

            // Player's saber vs enemy.
            Rect blade;
            if (getSaberHitbox(player, blade) && !player.saberHasHit && enemy.alive && overlaps(blade, enemy.box)) {
                damageEnemy(enemy, cfg::SABER_DMG[player.comboStep]); player.saberHasHit = true;
            }

            // Shots vs enemy / walls.
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

            // Enemy attack hitbox vs player.
            Rect ehb;
            if (getEnemyHitbox(enemy, ehb) && player.alive && overlaps(ehb, player.box)) {
                damagePlayer(player, cfg::E_ATTACK_DMG, (player.box.cx() < enemy.box.cx()) ? -1 : 1);
            }
            // Plain body contact also hurts (e.g. while chasing into you).
            if (enemy.alive && player.alive && overlaps(enemy.box, player.box)) {
                damagePlayer(player, cfg::E_TOUCH_DMG, (player.box.cx() < enemy.box.cx()) ? -1 : 1);
            }

            jumpPressed = dashPressed = saberPressed = false;
            accumulator -= cfg::FIXED_DT;
        }

        // --- AUDIO EVENTS ---
        // Detect "edges": state changes since last frame. This is how to map
        // gameplay events to sounds without spamming a sound every frame.

        // Jump: player just left the ground while moving upward.
        if (prevOnGround && !player.onGround && player.vy < 0) playSfx(audio.sJump);

        // Dash: started this frame.
        if (player.dashing && !prevDashing) playSfx(audio.sDash);

        // Saber: a new slash started (entered active phase, OR combo step advanced).
        bool slashStarted = (player.saberPhase == 1 && prevSaberPhase != 1)
                         || (player.saberPhase == 1 && player.comboStep != prevComboStep);
        if (slashStarted) playSfx(audio.sSaber);

        // Buster: new shot(s) appeared. Pick the right sound based on charged.
        if (shots.size() > prevShotCount) {
            // The newest shot is the one just pushed at the back.
            playSfx(shots.back().charged ? audio.sCharged : audio.sShot);
        }

        // Enemy hit: shrink in enemy HP since last frame (and still alive => hit-confirm).
        // Use the hitFlash going from 0->positive as a clean "just got hit" edge.
        static double prevEnemyFlash = 0;
        if (enemy.hitFlash > prevEnemyFlash + 0.001) playSfx(audio.sHit);
        prevEnemyFlash = enemy.hitFlash;

        // Enemy defeated: alive -> dead transition.
        if (prevEnemyAlive && !enemy.alive) playSfx(audio.sEnemy);

        // Player hurt: HP dropped.
        if (player.hp < prevPlayerHp) playSfx(audio.sHurt);

        // Update the snapshots for next frame's edge tests.
        prevSaberPhase = player.saberPhase;
        prevComboStep  = player.comboStep;
        prevPlayerHp   = player.hp;
        prevEnemyAlive = enemy.alive;
        prevShotCount  = shots.size();
        prevDashing    = player.dashing;
        prevOnGround   = player.onGround;

        // --- Render ---
        SDL_SetRenderDrawColor(ren, 24,26,38,255); SDL_RenderClear(ren);
        for (int ty=0; ty<lvl.height(); ++ty)
            for (int tx=0; tx<lvl.width(); ++tx)
                if (lvl.isSolid(tx,ty))
                    drawRect(ren, Rect{(double)tx*cfg::TILE,(double)ty*cfg::TILE,(double)cfg::TILE,(double)cfg::TILE}, 70,80,110);

        // Enemy + state-colored tint.
        if (enemy.alive) {
            Uint8 r=220,g=90,b=90; // patrol = red
            if (enemy.state==EState::Chase)  { r=240; g=150; b=60; }   // chase = orange
            if (enemy.state==EState::Attack) { r=255; g=210; b=60; }   // attack = yellow
            if (enemy.hitFlash>0)            { r=255; g=255; b=255; }  // hit = white
            drawRect(ren, enemy.box, r,g,b);
            // Windup telegraph: a thin marker on the side it's about to strike.
            if (enemy.state==EState::Attack && enemy.atkPhase==1) {
                double mx = enemy.facing>=0 ? enemy.box.right() : enemy.box.x-6;
                drawRect(ren, Rect{mx, enemy.box.y, 6, enemy.box.h}, 255,255,255,160);
            }
            // Enemy strike hitbox (visible while live).
            Rect ehb; if (getEnemyHitbox(enemy, ehb)) drawRect(ren, ehb, 255,120,120,170);
            // HP bar.
            double frac=(double)enemy.hp/cfg::DUMMY_HP;
            drawRect(ren, Rect{enemy.box.x,enemy.box.y-10,enemy.box.w,5}, 60,60,60);
            drawRect(ren, Rect{enemy.box.x,enemy.box.y-10,enemy.box.w*frac,5}, 90,220,120);
        }

        for (auto& s : shots) drawRect(ren, s.box, s.charged?120:255, s.charged?220:240, s.charged?255:160);

        // Player (flashes during i-frames so you can see invincibility).
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

        // Player HP bar (top-left).
        for (int i=0;i<cfg::PLAYER_MAX_HP;++i) {
            bool filled = i < player.hp;
            drawRect(ren, Rect{12.0+i*14, 12, 11, 16}, filled?90:50, filled?220:50, filled?255:60);
        }
        // Death prompt.
        if (!player.alive) drawRect(ren, Rect{cfg::SCREEN_W/2.0-80, cfg::SCREEN_H/2.0-12, 160, 24}, 200,60,60,200);

        SDL_RenderPresent(ren);
    }
    shutdownAudio(audio);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}