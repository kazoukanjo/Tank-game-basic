// tank_shooter_v2.cpp
// Tank Shooter v2.0 - Console ASCII upgrade
// Features:
// - Menu, instructions
// - Tank (big triangular 5x5), enemy 2x2, bullets
// - HP, Score, Levels
// - Colored output using ANSI VT sequences (enabled on Windows)
// - Double-buffer rendering (single write per frame)
// - Explosion effect (short animation)
// - Works on Windows & POSIX

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
const int FRAME_MS = 40;    // ~25 FPS
const int START_ENEMY_RATE = 40; // ticks between spawns (lower -> more enemies)
const int EXPLOSION_FRAMES = 4; // frames explosion visible

// ---------- Platform helpers ----------
#if defined(_WIN32) || defined(_WIN64)
static HANDLE gConsole = nullptr;
static DWORD gOutModeInit = 0;
#endif

void enableVTAndUTF8() {
#if defined(_WIN32) || defined(_WIN64)
  // enable virtual terminal processing for ANSI colors
  gConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (gConsole != INVALID_HANDLE_VALUE) {
    DWORD mode = 0;
    if (GetConsoleMode(gConsole, &mode)) {
      gOutModeInit = mode;
      mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
      SetConsoleMode(gConsole, mode);
    }
    // UTF-8 output code page
    SetConsoleOutputCP(CP_UTF8);
  }
#else
  // POSIX: nothing special required
#endif
}

void restoreConsole() {
#if defined(_WIN32) || defined(_WIN64)
  if (gConsole != INVALID_HANDLE_VALUE && gOutModeInit) {
    SetConsoleMode(gConsole, gOutModeInit);
  }
#endif
}

// Non-blocking keyboard helpers
void kb_init();
void kb_restore();
int kb_hit();
int kb_get();

#if defined(_WIN32) || defined(_WIN64)
void kb_init() { /* no-op for Windows */ }
void kb_restore() { /* no-op for Windows */ }
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

// ---------- Color helpers (ANSI escape sequences) ----------
string colorReset() { return "\x1B[0m"; }
string fgColor(int code) { // basic: 30-37 normal, 90-97 bright
  return "\x1B[" + to_string(code) + "m";
}
// pre-defined colors
const string COL_TANK = fgColor(92);    // bright green
const string COL_ENEMY = fgColor(91);   // bright red
const string COL_BULLET = fgColor(93);  // bright yellow
const string COL_BORDER = fgColor(37);  // white
const string COL_TEXT = fgColor(97);    // bright white
const string COL_EXPLOSION = fgColor(33);// orange-ish (yellow)

// ---------- Game entities ----------
struct Bullet { int x, y, dy; Bullet(int _x,int _y,int _dy):x(_x),y(_y),dy(_dy){} };
struct Enemy  { int x, y; int hp; Enemy(int _x,int _y,int _hp=1):x(_x),y(_y),hp(_hp){} };
struct Tank   { int x, y; int hp; Tank(int _x,int _y,int _hp=5):x(_x),y(_y),hp(_hp){} };
struct Explosion { int x, y; int life; Explosion(int _x,int _y,int _life):x(_x),y(_y),life(_life){} };

// ---------- State ----------
Tank player(WIDTH/2, HEIGHT-4, 5);
vector<Bullet> bullets;
vector<Enemy> enemies;
vector<Explosion> explosions;
int score = 0;
int tickCount = 0;
int enemySpawnRate = START_ENEMY_RATE;
int level = 1;
bool running = true;
atomic<bool> inMenu{true};

// ---------- Utility helpers ----------
inline void clampPos(int &x, int &y) {
  if (x < 2) x = 2;
  if (x > WIDTH-3) x = WIDTH-3;
  if (y < 2) y = 2;
  if (y > HEIGHT-4) y = HEIGHT-4;
}

// Build a blank buffer (vector<string>) sized WIDTHxHEIGHT
vector<string> createEmptyScreen() {
  return vector<string>(HEIGHT, string(WIDTH, ' '));
}

// Draw border with box characters and color codes in-line
void drawBorder(vector<string> &scr) {
  // top/bottom
  for (int x=0;x<WIDTH;x++) scr[0][x] = '-';
  for (int x=0;x<WIDTH;x++) scr[HEIGHT-1][x] = '-';
  // sides
  for (int y=0;y<HEIGHT;y++) {
    scr[y][0] = '|';
    scr[y][WIDTH-1] = '|';
  }
}

// ---------- Drawing functions (ASCII shapes) ----------
void drawTankShape(vector<string> &scr, const Tank &t) {
  // tank shaped triangular (center at t.x,t.y)
  // We'll draw with '*' but coloring is applied later via color markers in buffer string
  const vector<pair<int,int>> shape = {
    {0,0}, {-1,-1},{1,-1}, {-2,-2},{0,-2},{2,-2}, {-1,1},{1,1}, {0,-3}
  };
  for (auto &p : shape) {
    int nx = t.x + p.first, ny = t.y + p.second;
    if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '*';
  }
}

void drawEnemyShape(vector<string> &scr, const Enemy &e) {
  // enemy 2x2 block
  for (int dy=0; dy<2; ++dy)
    for (int dx=0; dx<2; ++dx) {
      int nx = e.x + dx, ny = e.y + dy;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '#';
    }
}

void drawBulletShape(vector<string> &scr, const Bullet &b) {
  if (b.x>=1 && b.x<WIDTH-1 && b.y>=1 && b.y<HEIGHT-1) scr[b.y][b.x] = '|';
}

void drawExplosions(vector<string> &scr) {
  for (auto &ex: explosions) {
    int r = EXPLOSION_FRAMES - ex.life + 1;
    // draw a small cross
    vector<pair<int,int>> parts = {{0,0},{-1,0},{1,0},{0,-1},{0,1}};
    for (auto &p: parts) {
      int nx = ex.x + p.first, ny = ex.y + p.second;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '@';
    }
  }
}

// ---------- Game subsystems ----------
void spawnEnemiesByLevel() {
  int cnt = 1 + rand() % min(3, level+1);
  for (int i=0;i<cnt;i++) {
    int x = 2 + rand() % (WIDTH-6);
    enemies.emplace_back(x, 2, 1);
  }
}

void processInputGameplay() {
  while (kb_hit()) {
    int c = kb_get();
    if (c == 0) break;
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == 'a') player.x--;
    else if (c == 'd') player.x++;
    else if (c == 'w') player.y--;
    else if (c == 's') player.y++;
    else if (c == ' ') bullets.emplace_back(player.x, player.y-4, -1);
    else if (c == 'q') running = false;
    else if (c == 'r') { // restart from game over quickly
      if (!running) { /* handled elsewhere */ }
    }
    clampPos(player.x, player.y);
  }
}

void updateGameLogic() {
  tickCount++;

  // bullets move
  for (auto &b: bullets) b.y += b.dy;
  bullets.erase(remove_if(bullets.begin(), bullets.end(), [](const Bullet &b){ return b.y < 1; }), bullets.end());

  // enemies move slower
  if (tickCount % max(1, 6 - level/2) == 0) {
    for (auto &e: enemies) e.y++;
  }

  // spawn by rate (adjusted by level)
  if (tickCount % max(8, enemySpawnRate - level*3) == 0) {
    spawnEnemiesByLevel();
  }

  // collisions: bullet vs enemy (enemy 2x2)
  vector<int> rmB, rmE;
  for (int i=0;i<(int)bullets.size();++i)
    for (int j=0;j<(int)enemies.size();++j) {
      if (abs(bullets[i].x - enemies[j].x) <= 1 && abs(bullets[i].y - enemies[j].y) <= 1) {
        rmB.push_back(i);
        rmE.push_back(j);
        score += 10;
        // spawn explosion at enemy center
        explosions.emplace_back(enemies[j].x, enemies[j].y, EXPLOSION_FRAMES);
      }
    }

  sort(rmB.rbegin(), rmB.rend());
  sort(rmE.rbegin(), rmE.rend());
  for (int i: rmB) if (i>=0 && i < (int)bullets.size()) bullets.erase(bullets.begin()+i);
  for (int j: rmE) if (j>=0 && j < (int)enemies.size()) enemies.erase(enemies.begin()+j);

  // explosions decay
  for (auto &ex: explosions) ex.life--;
  explosions.erase(remove_if(explosions.begin(), explosions.end(), [](const Explosion &e){ return e.life <= 0; }), explosions.end());

  // enemy vs player collision or enemy reaches bottom: damage or game over
  for (auto it = enemies.begin(); it != enemies.end();) {
    if (abs(it->x - player.x) <= 2 && abs(it->y - player.y) <= 2) {
      // collision: reduce HP and create explosion
      player.hp--;
      explosions.emplace_back(it->x, it->y, EXPLOSION_FRAMES);
      it = enemies.erase(it);
      if (player.hp <= 0) running = false;
    } else if (it->y >= HEIGHT-3) {
      // enemy reaches base -> reduce hp and remove
      player.hp--;
      explosions.emplace_back(it->x, it->y, EXPLOSION_FRAMES);
      it = enemies.erase(it);
      if (player.hp <= 0) running = false;
    } else ++it;
  }

  // level up by score
  if (score >= level * 200) {
    level++;
    // small reward
    player.hp = min(player.hp + 1, 7);
  }

  // cap vectors
  if ((int)enemies.size() > 200) enemies.erase(enemies.begin(), enemies.begin() + ((int)enemies.size() - 200));
  if ((int)bullets.size() > 300) bullets.erase(bullets.begin(), bullets.begin() + ((int)bullets.size() - 300));
}

// ---------- Rendering: prepare colored buffer string and output once ----------
string buildOutputBuffer(const vector<string> &scr) {
  // We'll insert color sequences inline:
  // - Borders: COL_BORDER
  // - Tank '*' -> COL_TANK
  // - Enemy '#' -> COL_ENEMY
  // - Bullet '|' -> COL_BULLET
  // - Explosion '@' -> COL_EXPLOSION
  string out;
  out.reserve(WIDTH * HEIGHT + 1024);

  // For speed, use one-pass mapping per character
  for (int y=0;y<HEIGHT;y++) {
    for (int x=0;x<WIDTH;x++) {
      char ch = scr[y][x];
      if (ch == '*' ) { out += COL_TANK; out.push_back('*'); out += colorReset(); }
      else if (ch == '#') { out += COL_ENEMY; out.push_back('#'); out += colorReset(); }
      else if (ch == '|') { out += COL_BULLET; out.push_back('|'); out += colorReset(); }
      else if (ch == '@') { out += COL_EXPLOSION; out.push_back('@'); out += colorReset(); }
      else if (ch == '|' || ch == '-' || ch == '|' ) { out.push_back(ch); } // fallback
      else if (ch == '-' || ch == '|' ) { out += COL_BORDER; out.push_back(ch); out += colorReset(); }
      else out.push_back(ch);
    }
    out.push_back('\n');
  }
  // status line
  out += COL_TEXT + string(" Score: ") + to_string(score) +
         "  HP: " + to_string(player.hp) +
         "  Level: " + to_string(level) +
         "  Enemies: " + to_string((int)enemies.size()) +
         "   (W/A/S/D move, Space shoot, Q quit)" + colorReset() + "\n";

  return out;
}

void renderScreen() {
  // prepare base screen
  auto scr = createEmptyScreen();
  drawBorder(scr);
  // draw entities (order: enemies, bullets, explosions, tank for layering)
  for (auto &e: enemies) drawEnemyShape(scr, e);
  for (auto &b: bullets) drawBulletShape(scr, b);
  drawExplosions(scr);
  drawTankShape(scr, player);

  // build colored buffer
  string buffer = buildOutputBuffer(scr);

  // output in one go
#if defined(_WIN32) || defined(_WIN64)
  // Move cursor to top-left then write using WriteConsoleA for speed
  if (gConsole == nullptr) gConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  COORD origin = {0,0};
  SetConsoleCursorPosition(gConsole, origin);
  DWORD written = 0;
  // WriteConsoleA might interpret some bytes differently for UTF-8; we enabled CP_UTF8 earlier.
  WriteConsoleA(gConsole, buffer.c_str(), (DWORD)buffer.size(), &written, NULL);
#else
  cout << "\x1B[H" << buffer << flush;
#endif
}

// ---------- Menu & flow ----------
void showTitleScreen() {
  // simple title with ascii art
  cout << "\x1B[2J\x1B[H"; // clear and home
  cout << COL_TEXT;
  cout << "=====================================\n";
  cout << "        TANK SHOOTER - v2.0          \n";
  cout << "=====================================\n\n";
  cout << "Controls: W/A/S/D - move    Space - shoot    Q - quit\n";
  cout << "Features: colored ASCII, explosions, HP, levels\n\n";
  cout << "1. Start Game\n2. Instructions\n3. Quit\n\n";
  cout << colorReset();
  cout << "Choose an option: ";
}

void showInstructions() {
  cout << "\x1B[2J\x1B[H";
  cout << COL_TEXT;
  cout << "Instructions:\n";
  cout << "- Move the tank with W A S D.\n";
  cout << "- Press Space to shoot upward.\n";
  cout << "- Destroy enemies (red 2x2 blocks) before they hit you.\n";
  cout << "- Gain score; every 200 points you level up.\n";
  cout << "- Survive as long as you can.\n\n";
  cout << "Press any key to return to menu.\n";
  cout << colorReset();
  // wait key
  while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
  kb_get();
}

void runGameLoop() {
  // initialize
  bullets.clear(); enemies.clear(); explosions.clear();
  score = 0; tickCount = 0; level = 1; player.hp = 5; running = true;

  // hide cursor (ANSI)
  cout << "\x1B[?25l";

  // initial spawn
  spawnEnemiesByLevel();

  while (running) {
    auto frameStart = chrono::steady_clock::now();
    processInputGameplay();
    updateGameLogic();
    renderScreen();
    // frame limit
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - frameStart).count();
    if (elapsed < FRAME_MS) this_thread::sleep_for(chrono::milliseconds(FRAME_MS - elapsed));
  }

  // show cursor again
  cout << "\x1B[?25h";

  // final message
  cout << "\x1B[2J\x1B[H";
  cout << COL_TEXT << "===== GAME OVER =====\n" << colorReset();
  cout << "Score: " << score << "   Level: " << level << "   HP: " << player.hp << "\n";
  cout << "Press R to restart or any other key to return to menu.\n";
  // wait for choice
  while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
  int c = kb_get();
  if (c == 'r' || c == 'R') {
    runGameLoop();
  }
}

// ---------- Main ----------
int main() {
  srand((unsigned)time(nullptr));
  enableVTAndUTF8();
  kb_init();

  // Main menu loop
  while (true) {
    showTitleScreen();
    // wait for input
    int opt = 0;
    while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
    opt = kb_get();
    if (opt == '1') {
      runGameLoop();
    } else if (opt == '2') {
      showInstructions();
    } else {
      // any other -> quit
      break;
    }
  }

  // restore console
  kb_restore();
  restoreConsole();
  cout << colorReset();
  cout << "\nGoodbye!\n";
  return 0;
}
