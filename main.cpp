// ============================================================================
//  Mega Man Zero / ZX-inspired game  —  movement core + COMBAT
//  Single-file SDL2 project (beginner-friendly, heavily commented)
//
//  NEW in this version (combat):
//    - Z-Saber: a 3-hit melee combo. Each press within the combo window
//      chains into the next slash; let the window lapse and it resets.
//    - Buster: tap to fire a small shot; HOLD to charge, release for a big shot.
//    - Hitboxes & hurtboxes: the core idea behind all action-game combat.
//    - A practice dummy enemy with health and a brief "hit flash" so you can
//      see damage land.
//
//  Controls:
//    Left / Right ... move          Z (hold) ... jump (release early = short hop)
//    X .............. dash/air-dash  C ......... saber slash  (tap-tap-tap to combo)
//    V (tap) ........ buster shot     V (hold then release) ... charged shot
//    R .............. respawn the dummy if you've destroyed it
//    Escape ......... quit
// ============================================================================

#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

// ----------------------------------------------------------------------------
//  Tunable constants. The "feel" lives here — tweak freely.
// ----------------------------------------------------------------------------
namespace cfg {
    constexpr int    SCREEN_W      = 960;
    constexpr int    SCREEN_H      = 540;
    constexpr int    TILE          = 32;
    constexpr double FIXED_DT      = 1.0 / 60.0;

    // Movement
    constexpr double MOVE_SPEED    = 220.0;
    constexpr double DASH_SPEED    = 430.0;
    constexpr double DASH_TIME     = 0.18;

    // Gravity & jump
    constexpr double GRAVITY       = 1500.0;
    constexpr double JUMP_VEL      = -470.0;
    constexpr double MAX_FALL      = 700.0;
    constexpr double JUMP_CUT      = 0.45;

    // Walls
    constexpr double WALL_SLIDE_SPEED = 110.0;
    constexpr double WALL_JUMP_X      = 300.0;
    constexpr double WALL_JUMP_Y      = -440.0;

    // Forgiveness windows
    constexpr double COYOTE_TIME   = 0.08;
    constexpr double JUMP_BUFFER   = 0.10;

    // ---- COMBAT tuning ----
    // Saber: each slash has a short "active" window where its hitbox can hit.
    constexpr double SABER_ACTIVE  = 0.12;  // how long the blade is "live" (sec)
    constexpr double SABER_RECOVER = 0.10;  // brief recovery after active frames
    constexpr double SABER_CHAIN   = 0.32;  // window to press again to chain combo
    constexpr int    SABER_DMG[3]  = { 3, 3, 5 }; // damage of hits 1, 2, 3
    constexpr double SABER_REACH   = 30.0;  // how far the blade extends from body
    constexpr double SABER_TALL    = 34.0;  // vertical size of the slash hitbox

    // Buster
    constexpr double SHOT_SPEED      = 520.0; // projectile speed px/sec
    constexpr double CHARGE_TIME     = 0.9;   // hold this long for a full charge
    constexpr int    SHOT_DMG_SMALL  = 1;
    constexpr int    SHOT_DMG_CHARGED= 4;
    constexpr double SHOT_LIFETIME   = 1.2;   // seconds before a shot despawns

    // Enemy
    constexpr int    DUMMY_HP        = 12;
    constexpr double HIT_FLASH_TIME  = 0.10;  // how long an enemy flashes white
}

// ----------------------------------------------------------------------------
//  AABB rectangle + overlap test.
// ----------------------------------------------------------------------------
struct Rect {
    double x, y, w, h;
    double right()  const { return x + w; }
    double bottom() const { return y + h; }
};

static bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() && a.right() > b.x &&
           a.y < b.bottom() && a.bottom() > b.y;
}

// ----------------------------------------------------------------------------
//  Level.
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
//  Player. Now carries combat state alongside the movement state.
// ----------------------------------------------------------------------------
struct Player {
    Rect box{ 96, 96, 22, 30 };
    double vx = 0, vy = 0;

    bool onGround = false, onWallLeft = false, onWallRight = false;
    int  facing = 1;

    bool   dashing = false;
    double dashTimer = 0;
    bool   usedAirDash = false;

    double coyoteTimer = 0, jumpBuffer = 0;
    bool   jumpHeld = false;

    // ---- Saber combo state ----
    // saberPhase: 0 = not attacking, 1 = active frames, 2 = recovery frames.
    int    saberPhase = 0;
    double saberTimer = 0;   // counts down within the current phase
    int    comboStep  = 0;   // which hit of the 3-hit combo we're on (0,1,2)
    double chainTimer = 0;   // time left to press again and chain the combo
    bool   saberHasHit= false; // has THIS slash already dealt its damage?
    bool   saberWantNext = false; // a press buffered mid-combo to chain next slash

    // ---- Buster charge state ----
    double chargeTime = 0;   // how long buster has been held
    bool   busterHeld = false;

    int hp = 16; // not used for damage yet, but here for later
};

// A projectile fired by the buster.
struct Shot {
    Rect box;
    double vx;
    double life;     // seconds remaining
    bool   charged;
    bool   alive = true;
};

// The practice dummy.
struct Enemy {
    Rect box{ 600, 96, 30, 46 };
    int  hp = cfg::DUMMY_HP;
    double hitFlash = 0; // >0 means show the white flash
    bool alive = true;
};

// ----------------------------------------------------------------------------
//  Collision (unchanged from the movement core).
// ----------------------------------------------------------------------------
static void moveAndCollide(Player& p, const Level& lvl, double dt) {
    p.box.x += p.vx * dt;
    p.onWallLeft = p.onWallRight = false;
    {
        int top    = (int)std::floor(p.box.y / cfg::TILE);
        int bottom = (int)std::floor((p.box.bottom() - 1) / cfg::TILE);
        if (p.vx > 0) {
            int tx = (int)std::floor((p.box.right() - 1) / cfg::TILE);
            for (int ty = top; ty <= bottom; ++ty)
                if (lvl.isSolid(tx, ty)) { p.box.x = tx * cfg::TILE - p.box.w; p.vx = 0; p.onWallRight = true; break; }
        } else if (p.vx < 0) {
            int tx = (int)std::floor(p.box.x / cfg::TILE);
            for (int ty = top; ty <= bottom; ++ty)
                if (lvl.isSolid(tx, ty)) { p.box.x = (tx + 1) * cfg::TILE; p.vx = 0; p.onWallLeft = true; break; }
        }
    }
    {
        int top    = (int)std::floor(p.box.y / cfg::TILE);
        int bottom = (int)std::floor((p.box.bottom() - 1) / cfg::TILE);
        int leftTx  = (int)std::floor((p.box.x - 1) / cfg::TILE);
        int rightTx = (int)std::floor((p.box.right()) / cfg::TILE);
        for (int ty = top; ty <= bottom; ++ty) {
            if (lvl.isSolid(leftTx, ty))  p.onWallLeft  = true;
            if (lvl.isSolid(rightTx, ty)) p.onWallRight = true;
        }
    }
    p.box.y += p.vy * dt;
    p.onGround = false;
    {
        int left  = (int)std::floor(p.box.x / cfg::TILE);
        int right = (int)std::floor((p.box.right() - 1) / cfg::TILE);
        if (p.vy > 0) {
            int ty = (int)std::floor((p.box.bottom() - 1) / cfg::TILE);
            for (int tx = left; tx <= right; ++tx)
                if (lvl.isSolid(tx, ty)) { p.box.y = ty * cfg::TILE - p.box.h; p.vy = 0; p.onGround = true; break; }
        } else if (p.vy < 0) {
            int ty = (int)std::floor(p.box.y / cfg::TILE);
            for (int tx = left; tx <= right; ++tx)
                if (lvl.isSolid(tx, ty)) { p.box.y = (ty + 1) * cfg::TILE; p.vy = 0; break; }
        }
    }
}

// ----------------------------------------------------------------------------
//  Compute the saber's hitbox for the current frame. Returns false when the
//  saber isn't in its "active" window (so there's no live blade to check).
//  This is the HITBOX: the rectangle that, while live, can damage enemies.
// ----------------------------------------------------------------------------
static bool getSaberHitbox(const Player& p, Rect& out) {
    if (p.saberPhase != 1) return false; // only the active phase has a live blade
    double cy = p.box.y + (p.box.h - cfg::SABER_TALL) * 0.5;
    if (p.facing >= 0)
        out = Rect{ p.box.right(), cy, cfg::SABER_REACH, cfg::SABER_TALL };
    else
        out = Rect{ p.box.x - cfg::SABER_REACH, cy, cfg::SABER_REACH, cfg::SABER_TALL };
    return true;
}

// ----------------------------------------------------------------------------
//  Movement update (same logic as before; saber/dash interplay noted inline).
// ----------------------------------------------------------------------------
static void updatePlayerMovement(Player& p, const Level& lvl, double dt,
                                 bool inLeft, bool inRight,
                                 bool jumpPressed, bool dashPressed) {
    if (p.coyoteTimer > 0) p.coyoteTimer -= dt;
    if (p.jumpBuffer  > 0) p.jumpBuffer  -= dt;
    if (jumpPressed) p.jumpBuffer = cfg::JUMP_BUFFER;

    bool wallTouch = (p.onWallLeft || p.onWallRight) && !p.onGround;

    if (dashPressed && !p.dashing) {
        if (p.onGround || !p.usedAirDash) {
            p.dashing = true; p.dashTimer = cfg::DASH_TIME;
            if (!p.onGround) p.usedAirDash = true;
            if (inLeft && !inRight)  p.facing = -1;
            if (inRight && !inLeft)  p.facing =  1;
        }
    }

    if (p.dashing) {
        p.dashTimer -= dt;
        p.vx = cfg::DASH_SPEED * p.facing;
        if (p.dashTimer <= 0) p.dashing = false;
    } else {
        if (inLeft && !inRight)  { p.vx = -cfg::MOVE_SPEED; p.facing = -1; }
        else if (inRight && !inLeft) { p.vx = cfg::MOVE_SPEED; p.facing = 1; }
        else p.vx = 0;
    }

    p.vy += cfg::GRAVITY * dt;

    bool slidingLeft  = p.onWallLeft  && inLeft  && p.vy > 0;
    bool slidingRight = p.onWallRight && inRight && p.vy > 0;
    if ((slidingLeft || slidingRight) && !p.onGround)
        if (p.vy > cfg::WALL_SLIDE_SPEED) p.vy = cfg::WALL_SLIDE_SPEED;

    if (p.vy > cfg::MAX_FALL) p.vy = cfg::MAX_FALL;

    if (p.jumpBuffer > 0) {
        if (p.onGround || p.coyoteTimer > 0) {
            p.vy = cfg::JUMP_VEL; p.jumpBuffer = 0; p.coyoteTimer = 0; p.dashing = false;
        } else if (wallTouch) {
            int away = p.onWallLeft ? 1 : -1;
            p.vx = cfg::WALL_JUMP_X * away; p.vy = cfg::WALL_JUMP_Y; p.facing = away;
            p.jumpBuffer = 0; p.usedAirDash = false; p.dashing = false;
        }
    }

    if (!p.jumpHeld && p.vy < 0) p.vy *= cfg::JUMP_CUT;

    moveAndCollide(p, lvl, dt);

    if (p.onGround) { p.coyoteTimer = cfg::COYOTE_TIME; p.usedAirDash = false; }
    if (p.onWallLeft || p.onWallRight) p.usedAirDash = false;
}

// ----------------------------------------------------------------------------
//  Saber combo update. This is a little state machine of its own:
//    phase 0 (idle): a press starts slash #1.
//    phase 1 (active): blade is live; pressing again is remembered to chain.
//    phase 2 (recovery): brief pause; if the chain window is open and a press
//                        was buffered, immediately start the next slash.
// ----------------------------------------------------------------------------
static void updateSaber(Player& p, double dt, bool saberPressed) {
    if (p.chainTimer > 0) p.chainTimer -= dt;

    if (saberPressed) {
        if (p.saberPhase == 0) {
            // Start the first slash of a fresh combo.
            p.comboStep   = 0;
            p.saberPhase  = 1;
            p.saberTimer  = cfg::SABER_ACTIVE;
            p.saberHasHit = false;
            p.chainTimer  = cfg::SABER_CHAIN;
        } else {
            // Mid-combo press: remember it; we'll chain at recovery's end.
            p.saberWantNext = true;
        }
    }

    if (p.saberPhase == 1) {           // active frames
        p.saberTimer -= dt;
        if (p.saberTimer <= 0) { p.saberPhase = 2; p.saberTimer = cfg::SABER_RECOVER; }
    } else if (p.saberPhase == 2) {    // recovery frames
        p.saberTimer -= dt;
        if (p.saberTimer <= 0) {
            // Decide whether to chain into the next slash.
            if (p.saberWantNext && p.chainTimer > 0 && p.comboStep < 2) {
                p.comboStep++;
                p.saberPhase  = 1;
                p.saberTimer  = cfg::SABER_ACTIVE;
                p.saberHasHit = false;
                p.chainTimer  = cfg::SABER_CHAIN;
            } else {
                p.saberPhase = 0; // combo ends
                p.comboStep  = 0;
            }
            p.saberWantNext = false;
        }
    }
}

// ----------------------------------------------------------------------------
//  Buster update: tap = small shot, hold-then-release = charged shot.
//  Returns a Shot via `out` and sets `fired` when a shot should spawn.
// ----------------------------------------------------------------------------
static void updateBuster(Player& p, double dt, bool busterDown,
                         bool& fired, Shot& out) {
    fired = false;
    if (busterDown) {
        p.chargeTime += dt;          // keep charging while held
        p.busterHeld = true;
    } else if (p.busterHeld) {
        // Released this frame: fire. Charged if held long enough.
        bool charged = p.chargeTime >= cfg::CHARGE_TIME;
        double size  = charged ? 18.0 : 8.0;
        double cy = p.box.y + p.box.h * 0.5 - size * 0.5;
        double sx = (p.facing >= 0) ? p.box.right() : p.box.x - size;
        out.box   = Rect{ sx, cy, size, size };
        out.vx    = cfg::SHOT_SPEED * p.facing;
        out.life  = cfg::SHOT_LIFETIME;
        out.charged = charged;
        out.alive = true;
        fired = true;

        p.chargeTime = 0;
        p.busterHeld = false;
    }
}

// ----------------------------------------------------------------------------
//  Apply a hit to the enemy: subtract damage, trigger the flash, handle death.
// ----------------------------------------------------------------------------
static void damageEnemy(Enemy& e, int dmg) {
    if (!e.alive) return;
    e.hp -= dmg;
    e.hitFlash = cfg::HIT_FLASH_TIME;
    if (e.hp <= 0) { e.hp = 0; e.alive = false; }
}

static void drawRect(SDL_Renderer* r, const Rect& b, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a = 255) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, a);
    SDL_Rect dst{ (int)std::lround(b.x), (int)std::lround(b.y), (int)std::lround(b.w), (int)std::lround(b.h) };
    SDL_RenderFillRect(r, &dst);
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
    SDL_Window* window = SDL_CreateWindow("MMZ-style: Movement + Combat",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cfg::SCREEN_W, cfg::SCREEN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Level  lvl = makeLevel();
    Player player;
    Enemy  dummy;
    std::vector<Shot> shots;

    bool prevJump=false, prevDash=false, prevSaber=false, prevReset=false;

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
        bool inLeft  = ks[SDL_SCANCODE_LEFT];
        bool inRight = ks[SDL_SCANCODE_RIGHT];
        bool jumpNow = ks[SDL_SCANCODE_Z];
        bool dashNow = ks[SDL_SCANCODE_X];
        bool saberNow= ks[SDL_SCANCODE_C];
        bool busterNow=ks[SDL_SCANCODE_V];
        bool resetNow= ks[SDL_SCANCODE_R];

        bool jumpPressed = jumpNow && !prevJump;
        bool dashPressed = dashNow && !prevDash;
        bool saberPressed= saberNow && !prevSaber;
        bool resetPressed= resetNow && !prevReset;
        prevJump=jumpNow; prevDash=dashNow; prevSaber=saberNow; prevReset=resetNow;
        player.jumpHeld = jumpNow;

        if (resetPressed && !dummy.alive) { dummy = Enemy{}; }

        Uint64 now = SDL_GetPerformanceCounter();
        double frameTime = (now - prevCounter) / freq;
        prevCounter = now;
        if (frameTime > 0.25) frameTime = 0.25;
        accumulator += frameTime;

        while (accumulator >= cfg::FIXED_DT) {
            const double dt = cfg::FIXED_DT;

            updatePlayerMovement(player, lvl, dt, inLeft, inRight, jumpPressed, dashPressed);
            updateSaber(player, dt, saberPressed);

            bool fired = false; Shot newShot;
            updateBuster(player, dt, busterNow, fired, newShot);
            if (fired) shots.push_back(newShot);

            // --- Saber vs enemy (hitbox/hurtbox overlap) ---
            Rect blade;
            if (getSaberHitbox(player, blade) && !player.saberHasHit && dummy.alive) {
                if (overlaps(blade, dummy.box)) {
                    damageEnemy(dummy, cfg::SABER_DMG[player.comboStep]);
                    player.saberHasHit = true; // one hit per slash
                }
            }

            // --- Advance shots & test against enemy ---
            for (auto& s : shots) {
                if (!s.alive) continue;
                s.box.x += s.vx * dt;
                s.life  -= dt;
                if (s.life <= 0) { s.alive = false; continue; }
                // Despawn on hitting a solid tile.
                int tx = (int)std::floor((s.box.x + s.box.w * 0.5) / cfg::TILE);
                int ty = (int)std::floor((s.box.y + s.box.h * 0.5) / cfg::TILE);
                if (lvl.isSolid(tx, ty)) { s.alive = false; continue; }
                if (dummy.alive && overlaps(s.box, dummy.box)) {
                    damageEnemy(dummy, s.charged ? cfg::SHOT_DMG_CHARGED : cfg::SHOT_DMG_SMALL);
                    s.alive = false;
                }
            }
            // Remove dead shots.
            shots.erase(std::remove_if(shots.begin(), shots.end(),
                        [](const Shot& s){ return !s.alive; }), shots.end());

            if (dummy.hitFlash > 0) dummy.hitFlash -= dt;

            jumpPressed = dashPressed = saberPressed = false; // edges fire once
            accumulator -= cfg::FIXED_DT;
        }

        // --- Render ---
        SDL_SetRenderDrawColor(ren, 24, 26, 38, 255);
        SDL_RenderClear(ren);

        for (int ty = 0; ty < lvl.height(); ++ty)
            for (int tx = 0; tx < lvl.width(); ++tx)
                if (lvl.isSolid(tx, ty))
                    drawRect(ren, Rect{(double)tx*cfg::TILE,(double)ty*cfg::TILE,(double)cfg::TILE,(double)cfg::TILE}, 70,80,110);

        // Enemy (flashes white when freshly hit).
        if (dummy.alive) {
            if (dummy.hitFlash > 0) drawRect(ren, dummy.box, 255,255,255);
            else                    drawRect(ren, dummy.box, 220,90,90);
            // Simple HP bar above it.
            double frac = (double)dummy.hp / cfg::DUMMY_HP;
            drawRect(ren, Rect{dummy.box.x, dummy.box.y-10, dummy.box.w, 5}, 60,60,60);
            drawRect(ren, Rect{dummy.box.x, dummy.box.y-10, dummy.box.w*frac, 5}, 90,220,120);
        }

        // Shots.
        for (auto& s : shots)
            if (s.charged) drawRect(ren, s.box, 120,220,255);
            else           drawRect(ren, s.box, 255,240,160);

        // Player (color shows state).
        Uint8 r=90,g=200,b=255;
        if (player.dashing)            { r=255; g=230; b=90; }
        else if (!player.onGround && (player.onWallLeft||player.onWallRight)) { r=255; g=130; b=200; }
        drawRect(ren, player.box, r,g,b);

        // Visualize the saber blade while active (semi-transparent white).
        Rect blade;
        if (getSaberHitbox(player, blade))
            drawRect(ren, blade, 230,255,255, 180);

        // Charge indicator: a small bar over the player that fills as you hold V.
        if (player.busterHeld) {
            double frac = player.chargeTime / cfg::CHARGE_TIME;
            if (frac > 1) frac = 1;
            Uint8 cr = frac>=1 ? 120 : 255, cg = frac>=1?220:240, cb = frac>=1?255:160;
            drawRect(ren, Rect{player.box.x, player.box.y-8, player.box.w*frac, 4}, cr,cg,cb);
        }

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}