// tank_game_smooth_v2.cpp
// Phiên bản mượt, tank 5x5, enemy 2x2, khung rộng.
// Chạy trên Windows & Linux/macOS.

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <ctime>
#include <algorithm>

#if defined(_WIN32) || defined(_WIN64)
  #include <conio.h>
  #include <windows.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

using namespace std;

const int WIDTH = 100;
const int HEIGHT = 30;
const int FRAME_MS = 50; // ~20 FPS

#if defined(_WIN32) || defined(_WIN64)
int kb_hit() { return _kbhit(); }
int kb_get() { return _getch(); }
void kb_init() {}
void kb_restore() {}
void setUTF8() { SetConsoleOutputCP(65001); }
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
void setUTF8() {}
#endif

struct Bullet { int x, y, dy; Bullet(int _x,int _y,int _dy):x(_x),y(_y),dy(_dy){} };
struct Enemy  { int x, y; Enemy(int _x,int _y):x(_x),y(_y){} };
struct Tank   { int x, y; Tank(int _x,int _y):x(_x),y(_y){} };

Tank player(WIDTH/2, HEIGHT-3);
vector<Bullet> bullets;
vector<Enemy> enemies;
int score = 0;
int tickCount = 0;
bool running = true;

void clampPos(int &x, int &y) {
  if (x < 3) x = 3;
  if (x > WIDTH-4) x = WIDTH-4;
  if (y < 3) y = 3;
  if (y > HEIGHT-4) y = HEIGHT-4;
}

void drawTank(vector<string> &scr, const Tank &t) {
  // Tank hình tam giác 5x5
  const vector<pair<int,int>> shape = {
    {0,0}, {-1,-1},{1,-1}, {-2,-2},{0,-2},{2,-2}, {-1,1},{1,1}, {0,-3}
  };
  for (auto &p : shape) {
    int nx = t.x + p.first, ny = t.y + p.second;
    if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1)
      scr[ny][nx] = '*';
  }
}

// === SỬA PHẦN NÀY ===
void drawEnemy(vector<string> &scr, const Enemy &e) {
  // Quái 2x2
  for (int dy=0; dy<2; ++dy)
    for (int dx=0; dx<2; ++dx) {
      int nx = e.x + dx, ny = e.y + dy;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1)
        scr[ny][nx] = '#';
    }
}

void drawBullet(vector<string> &scr, const Bullet &b) {
  if (b.x>=1 && b.x<WIDTH-1 && b.y>=1 && b.y<HEIGHT-1)
    scr[b.y][b.x] = '|';
}

void spawnEnemy() {
  int cnt = 1 + rand()%3;
  for (int i=0;i<cnt;i++)
    enemies.emplace_back(3 + rand()%(WIDTH-6), 2);
}

void processInput() {
  while (kb_hit()) {
    int c = kb_get();
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == 'a') player.x--;
    else if (c == 'd') player.x++;
    else if (c == 'w') player.y--;
    else if (c == 's') player.y++;
    else if (c == ' ') bullets.emplace_back(player.x, player.y-4, -1);
    else if (c == 'q') running = false;
    clampPos(player.x, player.y);
  }
}

void updateGame() {
  tickCount++;

  // move bullets
  for (auto &b : bullets) b.y += b.dy;
  bullets.erase(remove_if(bullets.begin(), bullets.end(),
              [](auto &b){ return b.y < 1; }), bullets.end());

  // move enemies
  if (tickCount % 4 == 0)
    for (auto &e : enemies) e.y++;

  // spawn enemies
  if (tickCount % 30 == 0)
    spawnEnemy();

  // bullet hits enemy (2x2 hitbox)
  vector<int> rmB, rmE;
  for (int i=0;i<(int)bullets.size();++i)
    for (int j=0;j<(int)enemies.size();++j)
      if (abs(bullets[i].x - enemies[j].x) <= 1 &&
          abs(bullets[i].y - enemies[j].y) <= 1) {
        rmB.push_back(i); rmE.push_back(j); score += 10;
      }

  sort(rmB.rbegin(), rmB.rend());
  sort(rmE.rbegin(), rmE.rend());
  for (int i : rmB) if (i>=0 && i<(int)bullets.size()) bullets.erase(bullets.begin()+i);
  for (int j : rmE) if (j>=0 && j<(int)enemies.size()) enemies.erase(enemies.begin()+j);

  // check lose (khoảng gần player)
  for (auto &e: enemies)
    if (abs(e.y - player.y) <= 2 && abs(e.x - player.x) <= 3)
      running = false;

  enemies.erase(remove_if(enemies.begin(), enemies.end(),
            [](auto &e){ return e.y>=HEIGHT-2; }), enemies.end());
}

#if defined(_WIN32) || defined(_WIN64)
static HANDLE gConsole = nullptr;
#endif

void renderBuffered(const vector<string> &scr) {
  string buffer;
  buffer.reserve(WIDTH * HEIGHT + 256);
  for (int y=0;y<HEIGHT;y++) {
    buffer += scr[y];
    buffer += '\n';
  }
  buffer += "Score: " + to_string(score) + " | W/A/S/D: di chuyển | Space: bắn | Q: thoát\n";

#if defined(_WIN32) || defined(_WIN64)
  if (gConsole == nullptr) gConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  COORD origin = {0,0};
  SetConsoleCursorPosition(gConsole, origin);
  DWORD written = 0;
  WriteConsoleA(gConsole, buffer.c_str(), (DWORD)buffer.size(), &written, NULL);
#else
  cout << "\033[H" << buffer << flush;
#endif
}

void render() {
  vector<string> scr(HEIGHT, string(WIDTH, ' '));
  for (int x=0;x<WIDTH;x++){ scr[0][x]='-'; scr[HEIGHT-1][x]='-'; }
  for (int y=0;y<HEIGHT;y++){ scr[y][0]='|'; scr[y][WIDTH-1]='|'; }

  for (auto &e: enemies) drawEnemy(scr, e);
  for (auto &b: bullets) drawBullet(scr, b);
  drawTank(scr, player);

  renderBuffered(scr);
}

int main() {
  srand((unsigned)time(nullptr));
  setUTF8();
  kb_init();

#if defined(_WIN32) || defined(_WIN64)
  gConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (gConsole != INVALID_HANDLE_VALUE) {
    CONSOLE_CURSOR_INFO cci;
    if (GetConsoleCursorInfo(gConsole, &cci)) {
      cci.bVisible = FALSE;
      SetConsoleCursorInfo(gConsole, &cci);
    }
  }
#endif

  player = Tank(WIDTH/2, HEIGHT-4);
  bullets.clear(); enemies.clear(); score=0; tickCount=0; running=true;

  spawnEnemy();

  while (running) {
    auto start = chrono::steady_clock::now();
    processInput();
    updateGame();
    render();
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - start).count();
    if (elapsed < FRAME_MS)
      this_thread::sleep_for(chrono::milliseconds(FRAME_MS - elapsed));
  }

  kb_restore();

#if defined(_WIN32) || defined(_WIN64)
  if (gConsole != INVALID_HANDLE_VALUE) {
    CONSOLE_CURSOR_INFO cci;
    if (GetConsoleCursorInfo(gConsole, &cci)) {
      cci.bVisible = TRUE;
      SetConsoleCursorInfo(gConsole, &cci);
    }
  }
#endif

  cout << "\n===== GAME OVER =====\n";
  cout << "Điểm của bạn: " << score << "\n";
  cout << "Cảm ơn đã chơi!\n";
  return 0;
}
