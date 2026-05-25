// ============================================================================
//  Mega Man Zero / ZX-inspired movement & physics core
//  Single-file SDL2 starter project (beginner-friendly, heavily commented)
//
//  What this demonstrates:
//    - Fixed-timestep game loop (deterministic physics, the key to good "feel")
//    - Tile-based level + AABB collision resolution
//    - A player state machine: idle, run, jump, dash, air-dash, wall-slide, wall-jump
//    - Variable jump height, coyote time, and jump buffering (modern responsiveness)
//
//  No textures yet — everything is drawn as colored rectangles so you can focus
//  on how the movement feels. Sprites come later.
//
//  Controls:
//    Left / Right arrows ........ move
//    Z (hold) ................... jump (release early for a shorter hop)
//    X .......................... dash (on ground) / air-dash (in air, once)
//    Escape ..................... quit
// ============================================================================

#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <cmath>

// ----------------------------------------------------------------------------
//  Tunable constants. Almost all the "feel" of the game lives here.
//  Units are pixels and seconds. Tweak these freely once it's running!
// ----------------------------------------------------------------------------
namespace cfg {
    constexpr int   SCREEN_W      = 960;
    constexpr int   SCREEN_H      = 540;
    constexpr int   TILE          = 32;     // size of one tile in pixels

    // The fixed timestep: physics advances in chunks of exactly this many
    // seconds, no matter how fast or slow the machine renders. 1/60 = 60 Hz.
    constexpr double FIXED_DT     = 1.0 / 60.0;

    // Horizontal movement
    constexpr double MOVE_SPEED   = 220.0;  // px/sec when running
    constexpr double DASH_SPEED   = 430.0;  // px/sec during a dash
    constexpr double DASH_TIME    = 0.18;   // how long a dash lasts (sec)

    // Gravity & jumping
    constexpr double GRAVITY      = 1500.0; // px/sec^2 pulling down
    constexpr double JUMP_VEL     = -470.0; // initial upward velocity (negative = up)
    constexpr double MAX_FALL     = 700.0;  // terminal velocity so falls aren't infinite

    // "Variable jump": when the player releases jump while still rising, we
    // cut the upward velocity so a tap = short hop, a hold = full jump.
    constexpr double JUMP_CUT     = 0.45;   // multiply upward velocity by this on release

    // Wall mechanics
    constexpr double WALL_SLIDE_SPEED = 110.0; // capped fall speed while hugging a wall
    constexpr double WALL_JUMP_X      = 300.0; // horizontal kick off the wall
    constexpr double WALL_JUMP_Y      = -440.0;// vertical kick off the wall

    // Forgiveness windows that make controls feel responsive (in seconds)
    constexpr double COYOTE_TIME  = 0.08;   // jump allowed shortly after leaving a ledge
    constexpr double JUMP_BUFFER  = 0.10;   // jump press remembered shortly before landing
}

// ----------------------------------------------------------------------------
//  A simple axis-aligned bounding box (AABB). Position is the top-left corner.
// ----------------------------------------------------------------------------
struct Rect {
    double x, y, w, h;
    double right()  const { return x + w; }
    double bottom() const { return y + h; }
};

// Returns true if two rectangles overlap.
static bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() && a.right() > b.x &&
           a.y < b.bottom() && a.bottom() > b.y;
}

// ----------------------------------------------------------------------------
//  The level. '#' is solid, anything else is empty space.
//  This is just a char grid — easy to read and edit by hand.
// ----------------------------------------------------------------------------
struct Level {
    std::vector<std::string> rows;
    int width()  const { return rows.empty() ? 0 : (int)rows[0].size(); }
    int height() const { return (int)rows.size(); }

    bool isSolid(int tx, int ty) const {
        if (ty < 0 || ty >= height() || tx < 0 || tx >= width())
            return true; // treat out-of-bounds as solid walls
        return rows[ty][tx] == '#';
    }
};

// A hand-built test level. Plenty of walls to practice wall-jumping.
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
//  The player. This holds physics state plus the small "memory" timers that
//  make jumping feel forgiving.
// ----------------------------------------------------------------------------
struct Player {
    Rect box{ 96, 96, 22, 30 };   // position + size (a bit smaller than a tile)
    double vx = 0, vy = 0;        // velocity in px/sec

    bool onGround   = false;
    bool onWallLeft = false;      // touching a wall on the left this frame
    bool onWallRight= false;      // touching a wall on the right this frame
    int  facing     = 1;          // 1 = right, -1 = left

    // Dash state
    bool   dashing      = false;
    double dashTimer    = 0;
    bool   usedAirDash  = false;  // only one air-dash until you touch ground/wall

    // Forgiveness timers
    double coyoteTimer  = 0;      // counts down after leaving the ground
    double jumpBuffer   = 0;      // counts down after a jump press

    bool jumpHeld = false;        // is the jump key currently held?
};

// ----------------------------------------------------------------------------
//  Collision: move the player one axis at a time and resolve against tiles.
//  Doing X and Y separately is the simplest reliable way to avoid the player
//  getting stuck on tile seams.
// ----------------------------------------------------------------------------
static void moveAndCollide(Player& p, const Level& lvl, double dt) {
    // --- Horizontal ---
    p.box.x += p.vx * dt;
    p.onWallLeft = p.onWallRight = false;
    {
        int top    = (int)std::floor(p.box.y / cfg::TILE);
        int bottom = (int)std::floor((p.box.bottom() - 1) / cfg::TILE);
        if (p.vx > 0) { // moving right: check the right edge
            int tx = (int)std::floor((p.box.right() - 1) / cfg::TILE);
            for (int ty = top; ty <= bottom; ++ty)
                if (lvl.isSolid(tx, ty)) {
                    p.box.x = tx * cfg::TILE - p.box.w;
                    p.vx = 0; p.onWallRight = true; break;
                }
        } else if (p.vx < 0) { // moving left: check the left edge
            int tx = (int)std::floor(p.box.x / cfg::TILE);
            for (int ty = top; ty <= bottom; ++ty)
                if (lvl.isSolid(tx, ty)) {
                    p.box.x = (tx + 1) * cfg::TILE;
                    p.vx = 0; p.onWallLeft = true; break;
                }
        }
    }

    // Even when standing still, detect an adjacent wall (for wall-slide/jump).
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

    // --- Vertical ---
    p.box.y += p.vy * dt;
    p.onGround = false;
    {
        int left  = (int)std::floor(p.box.x / cfg::TILE);
        int right = (int)std::floor((p.box.right() - 1) / cfg::TILE);
        if (p.vy > 0) { // falling: check the bottom edge
            int ty = (int)std::floor((p.box.bottom() - 1) / cfg::TILE);
            for (int tx = left; tx <= right; ++tx)
                if (lvl.isSolid(tx, ty)) {
                    p.box.y = ty * cfg::TILE - p.box.h;
                    p.vy = 0; p.onGround = true; break;
                }
        } else if (p.vy < 0) { // rising: check the top edge
            int ty = (int)std::floor(p.box.y / cfg::TILE);
            for (int tx = left; tx <= right; ++tx)
                if (lvl.isSolid(tx, ty)) {
                    p.box.y = (ty + 1) * cfg::TILE;
                    p.vy = 0; break;
                }
        }
    }
}

// ----------------------------------------------------------------------------
//  One fixed physics step. This is where the state machine logic lives.
//  `inLeft/inRight` = movement keys held this step.
//  `jumpPressed`    = jump key went down THIS step (edge, not hold).
//  `dashPressed`    = dash key went down this step.
// ----------------------------------------------------------------------------
static void updatePlayer(Player& p, const Level& lvl, double dt,
                         bool inLeft, bool inRight,
                         bool jumpPressed, bool dashPressed) {
    // Tick down the forgiveness timers.
    if (p.coyoteTimer > 0) p.coyoteTimer -= dt;
    if (p.jumpBuffer  > 0) p.jumpBuffer  -= dt;
    if (jumpPressed) p.jumpBuffer = cfg::JUMP_BUFFER; // remember the press

    bool wallTouch = (p.onWallLeft || p.onWallRight) && !p.onGround;

    // ---- Dash handling ----
    if (dashPressed && !p.dashing) {
        if (p.onGround || !p.usedAirDash) {
            p.dashing   = true;
            p.dashTimer = cfg::DASH_TIME;
            if (!p.onGround) p.usedAirDash = true; // consume the one air-dash
            // Dash in the facing direction (or the direction being pressed).
            if (inLeft && !inRight)  p.facing = -1;
            if (inRight && !inLeft)  p.facing =  1;
        }
    }

    if (p.dashing) {
        p.dashTimer -= dt;
        p.vx = cfg::DASH_SPEED * p.facing;
        if (p.dashTimer <= 0) p.dashing = false;
    } else {
        // ---- Normal horizontal movement ----
        if (inLeft && !inRight)  { p.vx = -cfg::MOVE_SPEED; p.facing = -1; }
        else if (inRight && !inLeft) { p.vx =  cfg::MOVE_SPEED; p.facing =  1; }
        else p.vx = 0;
    }

    // ---- Gravity ----
    p.vy += cfg::GRAVITY * dt;

    // Wall slide: if pressing into a wall while airborne and falling, cap speed.
    bool slidingLeft  = p.onWallLeft  && inLeft  && p.vy > 0;
    bool slidingRight = p.onWallRight && inRight && p.vy > 0;
    if ((slidingLeft || slidingRight) && !p.onGround) {
        if (p.vy > cfg::WALL_SLIDE_SPEED) p.vy = cfg::WALL_SLIDE_SPEED;
    }

    if (p.vy > cfg::MAX_FALL) p.vy = cfg::MAX_FALL; // terminal velocity

    // ---- Jump logic (uses the buffer + coyote time) ----
    bool wantJump = p.jumpBuffer > 0;
    if (wantJump) {
        if (p.onGround || p.coyoteTimer > 0) {
            // Normal ground jump
            p.vy = cfg::JUMP_VEL;
            p.jumpBuffer = 0;
            p.coyoteTimer = 0;
            p.dashing = false; // jumping cancels a dash
        } else if (wallTouch) {
            // Wall jump: launch away from the wall
            int away = p.onWallLeft ? 1 : -1;
            p.vx = cfg::WALL_JUMP_X * away;
            p.vy = cfg::WALL_JUMP_Y;
            p.facing = away;
            p.jumpBuffer = 0;
            p.usedAirDash = false; // wall contact refreshes the air-dash
            p.dashing = false;
        }
    }

    // ---- Variable jump height ----
    // If the player let go of jump while still moving up, cut the rise short.
    if (!p.jumpHeld && p.vy < 0) {
        p.vy *= cfg::JUMP_CUT;
    }

    // Move & resolve against the world.
    moveAndCollide(p, lvl, dt);

    // ---- Post-move bookkeeping ----
    if (p.onGround) {
        p.coyoteTimer = cfg::COYOTE_TIME; // refresh while grounded
        p.usedAirDash = false;            // landing refreshes the air-dash
    }
    if (p.onWallLeft || p.onWallRight) {
        p.usedAirDash = false;            // touching a wall also refreshes it
    }
}

// ----------------------------------------------------------------------------
//  Rendering. Everything is a colored rectangle for now.
// ----------------------------------------------------------------------------
static void drawRect(SDL_Renderer* r, const Rect& box,
                     Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect dst{ (int)std::lround(box.x), (int)std::lround(box.y),
                  (int)std::lround(box.w), (int)std::lround(box.h) };
    SDL_RenderFillRect(r, &dst);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Mega Man Zero-style Movement Core",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg::SCREEN_W, cfg::SCREEN_H, SDL_WINDOW_SHOWN);
    if (!window) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_Log("CreateRenderer failed: %s", SDL_GetError()); return 1; }

    Level  lvl    = makeLevel();
    Player player;

    // Edge-detection state for jump/dash (so holding doesn't re-trigger).
    bool prevJump = false, prevDash = false;

    // Fixed-timestep accumulator. We measure real elapsed time, add it to an
    // accumulator, and run as many fixed steps as fit. This decouples physics
    // from framerate — the heart of consistent "feel."
    Uint64 prevCounter = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    const double freq  = (double)SDL_GetPerformanceFrequency();

    bool running = true;
    while (running) {
        // --- Events ---
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                running = false;
        }

        // --- Read keyboard state ---
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        bool inLeft  = ks[SDL_SCANCODE_LEFT];
        bool inRight = ks[SDL_SCANCODE_RIGHT];
        bool jumpNow = ks[SDL_SCANCODE_Z];
        bool dashNow = ks[SDL_SCANCODE_X];

        // Convert "held" into "pressed this frame" (rising edge).
        bool jumpPressed = jumpNow && !prevJump;
        bool dashPressed = dashNow && !prevDash;
        prevJump = jumpNow;
        prevDash = dashNow;

        player.jumpHeld = jumpNow;

        // --- Advance simulation in fixed steps ---
        Uint64 now = SDL_GetPerformanceCounter();
        double frameTime = (now - prevCounter) / freq;
        prevCounter = now;
        if (frameTime > 0.25) frameTime = 0.25; // avoid spiral of death after a stall
        accumulator += frameTime;

        // The pressed-edge flags should only fire on the FIRST fixed step of
        // this frame, otherwise one tap could count multiple times.
        while (accumulator >= cfg::FIXED_DT) {
            updatePlayer(player, lvl, cfg::FIXED_DT,
                         inLeft, inRight, jumpPressed, dashPressed);
            jumpPressed = false;
            dashPressed = false;
            accumulator -= cfg::FIXED_DT;
        }

        // --- Render ---
        SDL_SetRenderDrawColor(ren, 24, 26, 38, 255); // dark background
        SDL_RenderClear(ren);

        // Draw level tiles.
        for (int ty = 0; ty < lvl.height(); ++ty)
            for (int tx = 0; tx < lvl.width(); ++tx)
                if (lvl.isSolid(tx, ty)) {
                    Rect t{ (double)tx * cfg::TILE, (double)ty * cfg::TILE,
                            (double)cfg::TILE, (double)cfg::TILE };
                    drawRect(ren, t, 70, 80, 110);
                }

        // Draw the player. Tint changes with state so you can see what's going on.
        Uint8 r = 90, g = 200, b = 255;          // default: cyan-ish (Zero!)
        if (player.dashing)            { r = 255; g = 230; b = 90; }  // dash = yellow
        else if (!player.onGround &&
                 (player.onWallLeft || player.onWallRight))
                                       { r = 255; g = 130; b = 200; } // wall = pink
        drawRect(ren, player.box, r, g, b);

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}