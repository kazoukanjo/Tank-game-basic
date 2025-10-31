// tank_shooter_v4_0.cpp
// Tank Shooter v4.0 - Phase 1: smarter enemies, improved graphics & explosions

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <string>
#include <atomic>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

using namespace std;

// ---------- Config ----------
const int WIDTH = 100;
const int HEIGHT = 30;
const int FRAME_MS = 40;
const int START_ENEMY_RATE = 40;
const int EXPLOSION_FRAMES = 6;

// ---------- Platform helpers ----------
#if defined(_WIN32) || defined(_WIN64)
static HANDLE gConsole = nullptr;
static DWORD gOutModeInit = 0;
#endif

void enableVTAndUTF8() {
#if defined(_WIN32) || defined(_WIN64)
  gConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (gConsole != INVALID_HANDLE_VALUE) {
    DWORD mode = 0;
    if (GetConsoleMode(gConsole, &mode)) {
      gOutModeInit = mode;
      mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
      SetConsoleMode(gConsole, mode);
    }
    SetConsoleOutputCP(CP_UTF8);
  }
#endif
}

void restoreConsole() {
#if defined(_WIN32) || defined(_WIN64)
  if (gConsole != INVALID_HANDLE_VALUE && gOutModeInit)
    SetConsoleMode(gConsole, gOutModeInit);
#endif
}

// ---------- Keyboard ----------
void kb_init();
void kb_restore();
int kb_hit();
int kb_get();

#if defined(_WIN32) || defined(_WIN64)
void kb_init() {}
void kb_restore() {}
int kb_hit() { return _kbhit(); }
int kb_get() { return _getch(); }
#else
static struct termios oldt;
void kb_init() {
  tcgetattr(STDIN_FILENO, &oldt);
  struct termios newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}
void kb_restore() {
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}
int kb_hit() {
  char ch;
  ssize_t n = read(STDIN_FILENO, &ch, 1);
  if (n == 1) { ungetc(ch, stdin); return 1; }
  return 0;
}
int kb_get() {
  int c = getchar();
  if (c == EOF) return 0;
  return c;
}
#endif

// ---------- Colors ----------
string colorReset() { return "\x1B[0m"; }
string fgColor(int code) { return "\x1B[" + to_string(code) + "m"; }
const string COL_TANK = fgColor(92);
const string COL_BULLET = fgColor(93);
const string COL_BORDER = fgColor(37);
const string COL_TEXT = fgColor(97);
const string COL_EXP1 = fgColor(33);
const string COL_EXP2 = fgColor(91);

// ---------- Entities ----------
struct Bullet { int x, y, dy; Bullet(int _x,int _y,int _dy):x(_x),y(_y),dy(_dy){} };

enum EnemyType { NORMAL, FAST, STRONG, BOUNCER, ZIGZAG, CHASER };
struct Enemy {
  int x, y, hp, dir;
  EnemyType type;
  Enemy(int _x,int _y,EnemyType _t):x(_x),y(_y),type(_t) {
    if (_t == NORMAL) hp = 1;
    else if (_t == FAST) hp = 1;
    else if (_t == STRONG) hp = 3;
    else if (_t == BOUNCER) hp = 2;
    else if (_t == ZIGZAG) hp = 2;
    else hp = 2; // CHASER
    dir = (rand()%2)?1:-1;
  }
};

struct Tank { int x, y; int hp; string type; int speed; int fireRate; };
struct Explosion { int x, y; int life; Explosion(int _x,int _y,int _life):x(_x),y(_y),life(_life){} };

// ---------- State ----------
Tank player{WIDTH/2, HEIGHT-4, 5, "Standard", 1, 6};
vector<Bullet> bullets;
vector<Enemy> enemies;
vector<Explosion> explosions;
int score = 0;
int tickCount = 0;
int enemySpawnRate = START_ENEMY_RATE;
int level = 1;
bool running = true;

// ---------- Utility ----------
inline void clampPos(int &x, int &y) {
  if (x < 2) x = 2;
  if (x > WIDTH-3) x = WIDTH-3;
  if (y < 2) y = 2;
  if (y > HEIGHT-4) y = HEIGHT-4;
}

vector<string> createEmptyScreen() { return vector<string>(HEIGHT, string(WIDTH, ' ')); }

void drawBorder(vector<string> &scr) {
  for (int x=0;x<WIDTH;x++) scr[0][x] = '-';
  for (int x=0;x<WIDTH;x++) scr[HEIGHT-1][x] = '-';
  for (int y=0;y<HEIGHT;y++) {
    scr[y][0] = '|';
    scr[y][WIDTH-1] = '|';
  }
}

// ---------- Drawings (rich ASCII per type) ----------
void drawTankShape(vector<string> &scr, const Tank &t) {
  vector<pair<int,int>> shape = {{0,0},{-1,-1},{1,-1},{-2,-2},{0,-2},{2,-2},{0,-3}};
  for (auto &p : shape) {
    int nx = t.x + p.first, ny = t.y + p.second;
    if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '*';
  }
}

// draw enemies with different shapes (keeps to 1-3 cell sizes)
void drawEnemyShape(vector<string> &scr, const Enemy &e) {
  bool blink = (tickCount/5)%2;
  switch (e.type) {
    case NORMAL: {
      // 2x2 box
      const char *s0 = "[]";
      const char *s1 = "[]";
      for (int dy=0; dy<2; ++dy)
        for (int dx=0; dx<2; ++dx) {
          int nx = e.x + dx, ny = e.y + dy;
          if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1)
            scr[ny][nx] = (dx==0 && dy==0) ? '#' : '#';
        }
      break;
    }
    case FAST: {
      // sleek arrow - blinks
      string s = blink ? "/^\\": "\\_/";
      int baseX = e.x - 1, baseY = e.y;
      for (int i=0;i<(int)s.size();++i) {
        int nx = baseX + i, ny = baseY;
        if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = s[i];
      }
      break;
    }
    case STRONG: {
      // 3x2 heavy block
      const string shape0 = "╔═╗";
      const string shape1 = "╚═╝";
      for (int dx=0; dx<3; ++dx) {
        int nx0 = e.x + dx - 1, ny0 = e.y - 1;
        int nx1 = e.x + dx - 1, ny1 = e.y;
        if (nx0>=1 && nx0<WIDTH-1 && ny0>=1 && ny0<HEIGHT-1) scr[ny0][nx0] = shape0[dx];
        if (nx1>=1 && nx1<WIDTH-1 && ny1>=1 && ny1<HEIGHT-1) scr[ny1][nx1] = shape1[dx];
      }
      break;
    }
    case BOUNCER: {
      // little bouncer with & center
      vector<pair<int,int>> parts = {{0,0},{-1,0},{1,0},{0,1}};
      for (auto &p: parts) {
        int nx = e.x + p.first, ny = e.y + p.second;
        if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = (p.first==0 && p.second==0) ? '&' : '=';
      }
      break;
    }
    case ZIGZAG: {
      // Z shape
      vector<pair<int,int>> parts = {{0,0},{1,1},{-1,1}};
      for (auto &p: parts) {
        int nx = e.x + p.first, ny = e.y + p.second;
        if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = 'Z';
      }
      break;
    }
    default: { // CHASER
      // chaser uses 'C'
      int nx = e.x, ny = e.y;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = 'C';
      break;
    }
  }
}

void drawBulletShape(vector<string> &scr, const Bullet &b) {
  if (b.x>=1 && b.x<WIDTH-1 && b.y>=1 && b.y<HEIGHT-1) scr[b.y][b.x] = '|';
}

// explosion fills few cells depending on phase
void drawExplosions(vector<string> &scr) {
  for (auto &ex: explosions) {
    int phase = ex.life % 3;
    vector<pair<int,int>> parts;
    if (phase == 0) parts = {{0,0}};
    else if (phase == 1) parts = {{0,0},{-1,0},{1,0},{0,-1},{0,1}};
    else parts = {{-1,-1},{1,-1},{-1,1},{1,1}};
    for (auto &p: parts) {
      int nx = ex.x + p.first, ny = ex.y + p.second;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1)
        scr[ny][nx] = '*';
    }
  }
}

// ---------- Logic ----------
void spawnEnemiesByLevel() {
  int cnt = 1 + rand() % min(4, level + 1);
  for (int i=0;i<cnt;i++) {
    int r = rand() % 100;
    EnemyType t;
    if (r < 40) t = NORMAL;
    else if (r < 60) t = FAST;
    else if (r < 75) t = STRONG;
    else if (r < 88) t = BOUNCER;
    else if (r < 96) t = ZIGZAG;
    else t = CHASER;
    enemies.emplace_back(2 + rand() % (WIDTH - 6), 2 + rand()%2, t);
  }
}

void processInputGameplay() {
  static int shootCooldown = 0;
  shootCooldown = max(0, shootCooldown - 1);

  while (kb_hit()) {
    int c = kb_get();
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == 'a') player.x -= player.speed;
    else if (c == 'd') player.x += player.speed;
    else if (c == 'w') player.y -= player.speed;
    else if (c == 's') player.y += player.speed;
    else if (c == ' ' && shootCooldown == 0) {
      bullets.emplace_back(player.x, player.y-4, -1);
      shootCooldown = player.fireRate;
    }
    else if (c == 'q') running = false;
    clampPos(player.x, player.y);
  }
}

void updateGameLogic() {
  tickCount++;

  // move bullets
  for (auto &b: bullets) b.y += b.dy;
  bullets.erase(remove_if(bullets.begin(), bullets.end(),
    [](const Bullet &b){ return b.y < 1; }), bullets.end());

  // Determine a global enemy move delay (slower overall)
  int globalDelay = max(2, 10 - level/2); // larger -> slower
  // per-type speed multiplier (1 = moves when tick % globalDelay == 0)
  for (auto &e : enemies) {
    bool doMove = false;
    if (e.type == FAST) {
      // fast moves a bit more often, but we reduced overall speed: move when tick%max(1,globalDelay/2)==0
      int d = max(1, globalDelay/2);
      doMove = (tickCount % d) == 0;
    } else if (e.type == CHASER) {
      // chaser moves slightly slower but will home towards player when in range
      doMove = (tickCount % globalDelay) == 0;
    } else {
      doMove = (tickCount % globalDelay) == 0;
    }

    if (!doMove) continue;

    switch (e.type) {
      case FAST:
        e.y += 1; // reduced from 2 -> 1
        break;
      case STRONG:
        e.y += 1;
        break;
      case NORMAL:
        e.y += 1;
        break;
      case BOUNCER:
        e.y += 1;
        e.x += e.dir;
        if (e.x <= 2 || e.x >= WIDTH-3) e.dir *= -1;
        break;
      case ZIGZAG:
        e.y += 1;
        e.x += e.dir;
        if (tickCount % 12 == 0) e.dir *= -1;
        if (e.x <= 2 || e.x >= WIDTH-3) e.dir *= -1;
        break;
      case CHASER: {
        // If within horizontal range, move toward player
        int dx = player.x - e.x;
        int dy = player.y - e.y;
        if (abs(dx) <= 20 && abs(dy) <= 10) {
          // home in
          e.x += (dx==0?0: (dx>0?1:-1));
          e.y += (dy==0?0: (dy>0?1:-1));
        } else {
          // fallback slow descent
          e.y += 1;
        }
        break;
      }
    }
  }

  // spawn
  if (tickCount % max(8, enemySpawnRate - level*3) == 0)
    spawnEnemiesByLevel();

  // bullet vs enemy collisions
  vector<int> rmB, rmE;
  for (int i=0;i<(int)bullets.size();++i) {
    for (int j=0;j<(int)enemies.size();++j) {
      // collision threshold depends on enemy size; use <=1
      if (abs(bullets[i].x - enemies[j].x) <= 1 && abs(bullets[i].y - enemies[j].y) <= 1) {
        rmB.push_back(i);
        enemies[j].hp--;
        explosions.emplace_back(bullets[i].x, bullets[i].y, EXPLOSION_FRAMES/2);
        if (enemies[j].hp <= 0) {
          rmE.push_back(j);
          score += 10;
          explosions.emplace_back(enemies[j].x, enemies[j].y, EXPLOSION_FRAMES);
        }
      }
    }
  }

  sort(rmB.rbegin(), rmB.rend());
  sort(rmE.rbegin(), rmE.rend());
  for (int i: rmB) if (i < (int)bullets.size()) bullets.erase(bullets.begin()+i);
  for (int j: rmE) if (j < (int)enemies.size()) enemies.erase(enemies.begin()+j);

  // update explosions
  for (auto &ex: explosions) ex.life--;
  explosions.erase(remove_if(explosions.begin(), explosions.end(),
    [](const Explosion &e){ return e.life <= 0; }), explosions.end());

  // player collision -> immediate end (as you requested)
  for (auto &e : enemies) {
    // use bigger threshold for larger enemies
    int thresh = (e.type==STRONG)?3:2;
    if (abs(e.x - player.x) <= thresh && abs(e.y - player.y) <= thresh) {
      explosions.emplace_back(e.x, e.y, EXPLOSION_FRAMES);
      explosions.emplace_back(player.x, player.y, EXPLOSION_FRAMES);
      running = false;
      return;
    }
  }

  // remove enemies that pass bottom
  enemies.erase(remove_if(enemies.begin(), enemies.end(),
    [](const Enemy &e){ return e.y >= HEIGHT-3; }), enemies.end());

  // level up by score
  if (score >= level * 200) {
    level++;
    player.hp = min(player.hp + 1, 9);
    // small visual reward: spawn a couple of enemies away from player
    for (int i=0;i<2;i++) spawnEnemiesByLevel();
  }
}

// ---------- Rendering ----------
string buildOutputBuffer(const vector<string> &scr) {
  // Count enemy types for HUD
  int cntNormal=0,cntFast=0,cntStrong=0,cntBouncer=0,cntZig=0,cntChaser=0;
  for (auto &e: enemies) {
    switch (e.type) {
      case NORMAL: cntNormal++; break;
      case FAST: cntFast++; break;
      case STRONG: cntStrong++; break;
      case BOUNCER: cntBouncer++; break;
      case ZIGZAG: cntZig++; break;
      case CHASER: cntChaser++; break;
    }
  }

  string out;
  out.reserve(WIDTH * HEIGHT + 256);
  for (int y=0;y<HEIGHT;y++) {
    for (int x=0;x<WIDTH;x++) {
      char ch = scr[y][x];
      if (ch == '*') {
        // explosion/pulse: alternate colors by tickCount
        if ((tickCount/2)%2==0) out += COL_EXP1 + "*" + colorReset();
        else out += COL_EXP2 + "*" + colorReset();
      }
      else if (ch == '#') out += fgColor(91) + "#" + colorReset();        // normal
      else if (ch == '/' || ch == '\\' || ch == '_' || ch == '^') out += fgColor(95) + string(1,ch) + colorReset(); // fast blink
      else if (ch == '╔' || ch == '═' || ch == '╚' || ch == '╝') out += fgColor(34) + string(1,ch) + colorReset(); // strong in blue
      else if (ch == '=') out += fgColor(93) + "=" + colorReset();
      else if (ch == '&') out += fgColor(93) + "&" + colorReset();
      else if (ch == 'Z') out += fgColor(96) + "Z" + colorReset();
      else if (ch == 'C') out += fgColor(95) + "C" + colorReset();
      else if (ch == '|') out += COL_BULLET + "|" + colorReset();
      else if (ch == '-' || ch == '|') out += COL_BORDER + ch + colorReset();
      else out.push_back(ch);
    }
    out.push_back('\n');
  }

  // HUD line with enemy counts (compact)
  out += COL_TEXT + string(" Tank: ") + player.type +
         " | Score: " + to_string(score) +
         " | HP: " + to_string(player.hp) +
         " | Level: " + to_string(level) +
         " | Enemies: " + to_string((int)enemies.size())
         + " (N:" + to_string(cntNormal)
         + " F:" + to_string(cntFast)
         + " S:" + to_string(cntStrong)
         + " B:" + to_string(cntBouncer)
         + " Z:" + to_string(cntZig)
         + " C:" + to_string(cntChaser)
         + ")   (W/A/S/D move, Space shoot, Q quit)"
         + colorReset() + "\n";

  return out;
}

void renderScreen() {
  auto scr = createEmptyScreen();
  drawBorder(scr);
  for (auto &e: enemies) drawEnemyShape(scr, e);
  for (auto &b: bullets) drawBulletShape(scr, b);
  drawExplosions(scr);
  drawTankShape(scr, player);

  string buffer = buildOutputBuffer(scr);
#if defined(_WIN32) || defined(_WIN64)
  if (gConsole == nullptr) gConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  COORD origin = {0,0};
  SetConsoleCursorPosition(gConsole, origin);
  DWORD written = 0;
  WriteConsoleA(gConsole, buffer.c_str(), (DWORD)buffer.size(), &written, NULL);
#else
  cout << "\x1B[H" << buffer << flush;
#endif
}

// ---------- Menu ----------
int chooseTank() {
  cout << "\x1B[2J\x1B[H" << COL_TEXT;
  cout << "===== CHOOSE YOUR TANK =====\n\n";
  cout << "1. Standard Tank  - HP:5  Speed:1  FireRate:6 (Balanced)\n";
  cout << "2. Heavy Tank     - HP:8  Speed:1  FireRate:8 (Strong but slow)\n";
  cout << "3. Light Tank     - HP:3  Speed:2  FireRate:4 (Fast, weak)\n\n";
  cout << "Choose 1, 2, or 3: " << colorReset();
  int choice = 1;
  while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
  choice = kb_get();
  if (choice == '2') return 2;
  if (choice == '3') return 3;
  return 1;
}

void showTitleScreen() {
  cout << "\x1B[2J\x1B[H" << COL_TEXT;
  cout << "=====================================\n";
  cout << "        TANK SHOOTER - v4.0          \n";
  cout << "=====================================\n\n";
  cout << "1. Start Game\n2. Instructions\n3. Quit\n\n";
  cout << colorReset() << "Choose an option: ";
}

void showInstructions() {
  cout << "\x1B[2J\x1B[H" << COL_TEXT;
  cout << "Instructions:\n";
  cout << "- Move with W/A/S/D.\n";
  cout << "- Shoot with Space.\n";
  cout << "- Avoid or destroy enemies before they hit you.\n";
  cout << "- New enemy types (v4.0):\n";
  cout << "   # Normal   - balanced\n";
  cout << "   /\\ or \\_/  - Fast (sleek)\n";
  cout << "   ╔═╗...     - Strong (armored)\n";
  cout << "   & Bouncer  - moves side to side\n";
  cout << "   Z Zigzag   - erratic movement\n";
  cout << "   C Chaser   - homes in when near\n\n";
  cout << "Press any key to return.\n" << colorReset();
  while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
  kb_get();
}

// ---------- Game loop ----------
void runGameLoop() {
  bullets.clear(); enemies.clear(); explosions.clear();
  score = 0; tickCount = 0; level = 1; enemySpawnRate = START_ENEMY_RATE;
  running = true;

  int choice = chooseTank();
  if (choice == 1) player = {WIDTH/2, HEIGHT-4, 5, "Standard", 1, 6};
  else if (choice == 2) player = {WIDTH/2, HEIGHT-4, 8, "Heavy", 1, 8};
  else player = {WIDTH/2, HEIGHT-4, 3, "Light", 2, 4};

  cout << "\x1B[?25l"; // hide cursor
  spawnEnemiesByLevel();

  while (running) {
    auto frameStart = chrono::steady_clock::now();
    processInputGameplay();
    updateGameLogic();
    renderScreen();
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(
      chrono::steady_clock::now() - frameStart).count();
    if (elapsed < FRAME_MS) this_thread::sleep_for(chrono::milliseconds(FRAME_MS - elapsed));
  }

  cout << "\x1B[?25h"; // show cursor
  cout << "\x1B[2J\x1B[H" << COL_TEXT;
  cout << "\n💥 GAME OVER 💥\n\n";
  cout << "Final Score: " << score << "\n";
  cout << "Level Reached: " << level << "\n";
  cout << "Press 'r' to restart or any key to return.\n";
  while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
  int c = kb_get();
  if (c == 'r' || c == 'R') runGameLoop();
}

// ---------- Main ----------
int main() {
  srand((unsigned)time(nullptr));
  enableVTAndUTF8();
  kb_init();

  while (true) {
    showTitleScreen();
    while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
    int opt = kb_get();
    if (opt == '1') runGameLoop();
    else if (opt == '2') showInstructions();
    else break;
  }

  kb_restore();
  restoreConsole();
  cout << colorReset() << "\nGoodbye!\n";
  return 0;
}
