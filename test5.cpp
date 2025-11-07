// tank_shooter_v5_powerups.cpp
// Tank Shooter v5 - PowerUps + Boss Skills (Laser & Bomb Rain)
// Merged from previous v4 with added features: items, power-ups, boss skills

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
const string COL_BOSS = fgColor(35);

// ---------- Entities ----------
struct Bullet {
  int x, y, dy, dmg;
  char ch;
  Bullet(int _x,int _y,int _dy,int _dmg,char _ch):x(_x),y(_y),dy(_dy),dmg(_dmg),ch(_ch){}
};

enum EnemyType { NORMAL, FAST, STRONG, BOUNCER, ZIGZAG, CHASER, BOSS };
struct Enemy {
  int x, y, hp, dir;
  EnemyType type;
  int skillCooldown; // for boss abilities
  Enemy(int _x,int _y,EnemyType _t):x(_x),y(_y),type(_t) {
    if (_t == NORMAL) hp = 1;
    else if (_t == FAST) hp = 1;
    else if (_t == STRONG) hp = 3;
    else if (_t == BOUNCER) hp = 2;
    else if (_t == ZIGZAG) hp = 2;
    else if (_t == BOSS) hp = 20;
    else hp = 2; // CHASER
    dir = (rand()%2)?1:-1;
    skillCooldown = 0;
  }
};

struct Tank {
  int x, y;
  int hp;
  string type;
  int speed;
  int fireRate;
  int shotDamage;
  int shotCount;
  int shieldCount;
  Tank(int _x=0,int _y=0,int _hp=5,string _type="Standard",int _speed=1,int _fireRate=6,
       int _shotDamage=1,int _shotCount=1,int _shieldCount=0)
    : x(_x), y(_y), hp(_hp), type(_type), speed(_speed), fireRate(_fireRate),
      shotDamage(_shotDamage), shotCount(_shotCount), shieldCount(_shieldCount) {}
};

struct Explosion { int x, y, life; Explosion(int _x,int _y,int _life):x(_x),y(_y),life(_life){} };

// Item / Power-up
enum ItemType { IT_HEALTH, IT_SHIELD, IT_RAPID, IT_DAMAGE };
struct Item {
  int x,y;
  ItemType t;
  int life; // how many ticks before disappear
  char ch;
  Item(int _x,int _y, ItemType _t):x(_x),y(_y),t(_t),life(400) {
    if (t==IT_HEALTH) ch = '+';
    else if (t==IT_SHIELD) ch = 'S';
    else if (t==IT_RAPID) ch = 'R';
    else ch = 'D';
  }
};

// Bomb (boss bomb rain)
struct Bomb {
  int x,y,dy;
  Bomb(int _x,int _y,int _dy):x(_x),y(_y),dy(_dy){}
};

// Laser beam active
struct LaserBeam {
  int y;
  int life;
  bool active;
  LaserBeam():y(0),life(0),active(false){}
};

// ---------- State ----------
Tank player{WIDTH/2, HEIGHT-4, 5, "Standard", 1, 6, 1, 1, 0};
vector<Bullet> bullets;
vector<Enemy> enemies;
vector<Explosion> explosions;
vector<Item> items;
vector<Bomb> bombs;
LaserBeam laser;

int score = 0;
int tickCount = 0;
int enemySpawnRate = START_ENEMY_RATE;
int level = 1;
bool running = true;

// Power-up runtime states
int rapidFireTimer = 0;     // ticks remaining
int damageBoostTimer = 0;   // ticks remaining

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

// ---------- Drawings ----------
void drawTankShape(vector<string> &scr, const Tank &t) {
  if (t.type == "Standard") {
    vector<pair<int,int>> shape = {{0,0},{-1,-1},{1,-1},{-2,-2},{0,-2},{2,-2},{0,-3}};
    for (auto &p : shape) {
      int nx = t.x + p.first, ny = t.y + p.second;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '*';
    }
  } else if (t.type == "Heavy") {
    vector<pair<int,int>> shape = {{0,0},{-1,0},{1,0},{-2,-1},{-1,-1},{0,-1},{1,-1},{2,-1}};
    for (auto &p : shape) {
      int nx = t.x + p.first, ny = t.y + p.second;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '#';
    }
  } else if (t.type == "Light") {
    vector<pair<int,int>> shape = {{0,0},{0,-1},{-1,0},{1,0},{0,1}};
    for (auto &p : shape) {
      int nx = t.x + p.first, ny = t.y + p.second;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '+';
    }
  } else if (t.type == "Sniper") {
    vector<pair<int,int>> shape = {{0,-2},{0,0},{0,-1},{-1,0},{1,0}};
    for (auto &p : shape) {
      int nx = t.x + p.first, ny = t.y + p.second;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = (p.first==0 && p.second==-2) ? '^' : (p.first==0 && p.second==-1 ? '^' : (p.first==0 && p.second==0 ? 'v' : '|'));
    }
  } else if (t.type == "RapidFire") {
    vector<pair<int,int>> shape = {{-1,0},{0,0},{1,0},{0,-1},{0,-2}};
    for (auto &p : shape) {
      int nx = t.x + p.first, ny = t.y + p.second;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '=';
    }
  } else if (t.type == "Plasma") {
    vector<pair<int,int>> shape = {{0,0},{-1,-1},{1,-1},{-1,1},{1,1}};
    for (auto &p : shape) {
      int nx = t.x + p.first, ny = t.y + p.second;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = (p.first==0 && p.second==0) ? 'O' : 'o';
    }
  } else {
    int nx = t.x, ny = t.y;
    if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = '*';
  }
}

void drawEnemyShape(vector<string> &scr, const Enemy &e) {
  bool blink = (tickCount/5)%2;
  switch (e.type) {
    case NORMAL: {
      for (int dy=0; dy<2; ++dy)
        for (int dx=0; dx<2; ++dx) {
          int nx = e.x + dx, ny = e.y + dy;
          if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1)
            scr[ny][nx] = '#';
        }
      break;
    }
    case FAST: {
      string s = blink ? "/^\\" : "\\_/";
      int baseX = e.x - 1, baseY = e.y;
      for (int i=0;i<(int)s.size();++i) {
        int nx = baseX + i, ny = baseY;
        if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = s[i];
      }
      break;
    }
    case STRONG: {
      const string shape0 = "+-+";
      const string shape1 = "+-+";
      for (int dx=0; dx<3; ++dx) {
        int nx0 = e.x + dx - 1, ny0 = e.y - 1;
        int nx1 = e.x + dx - 1, ny1 = e.y;
        if (nx0>=1 && nx0<WIDTH-1 && ny0>=1 && ny0<HEIGHT-1) scr[ny0][nx0] = shape0[dx];
        if (nx1>=1 && nx1<WIDTH-1 && ny1>=1 && ny1<HEIGHT-1) scr[ny1][nx1] = shape1[dx];
      }
      break;
    }
    case BOUNCER: {
      vector<pair<int,int>> parts = {{0,0},{-1,0},{1,0},{0,1}};
      for (auto &p: parts) {
        int nx = e.x + p.first, ny = e.y + p.second;
        if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = (p.first==0 && p.second==0) ? '&' : '=';
      }
      break;
    }
    case ZIGZAG: {
      vector<pair<int,int>> parts = {{0,0},{1,1},{-1,1}};
      for (auto &p: parts) {
        int nx = e.x + p.first, ny = e.y + p.second;
        if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = 'Z';
      }
      break;
    }
    case CHASER: {
      int nx = e.x, ny = e.y;
      if (nx>=1 && nx<WIDTH-1 && ny>=1 && ny<HEIGHT-1) scr[ny][nx] = 'C';
      break;
    }
    case BOSS: {
      // 5x2 boss
      string s0="+---+", s1="+---+";
      for (int dx=0; dx<5; ++dx) {
        int nx0 = e.x + dx - 2, ny0 = e.y;
        int nx1 = e.x + dx - 2, ny1 = e.y + 1;
        if (nx0>=1 && nx0<WIDTH-1 && ny0>=1 && ny0<HEIGHT-1) scr[ny0][nx0] = s0[dx];
        if (nx1>=1 && nx1<WIDTH-1 && ny1>=1 && ny1<HEIGHT-1) scr[ny1][nx1] = s1[dx];
      }
      break;
    }
  }
}

void drawBulletShape(vector<string> &scr, const Bullet &b) {
  if (b.x>=1 && b.x<WIDTH-1 && b.y>=1 && b.y<HEIGHT-1) scr[b.y][b.x] = b.ch;
}

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

void drawItems(vector<string> &scr) {
  for (auto &it: items) {
    if (it.x>=1 && it.x<WIDTH-1 && it.y>=1 && it.y<HEIGHT-1) scr[it.y][it.x] = it.ch;
  }
}

void drawBombs(vector<string> &scr) {
  for (auto &b: bombs) {
    if (b.x>=1 && b.x<WIDTH-1 && b.y>=1 && b.y<HEIGHT-1) scr[b.y][b.x] = 'o';
  }
}

void drawLaser(vector<string> &scr) {
  if (laser.active && laser.life>0) {
    int y = laser.y;
    if (y>=1 && y<HEIGHT-1) {
      for (int x=1;x<WIDTH-1;++x) scr[y][x] = '-';
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

void spawnBoss(){
    int x = WIDTH/2;
    int y = 2;
    Enemy e(x,y,BOSS);
    e.hp = 20 + level * 5; // boss scales by level
    e.dir = 1;
    e.skillCooldown = 0;
    enemies.push_back(e);
}

// drop item with small chance on enemy death
void maybeDropItem(int x,int y) {
  int r = rand()%100;
  if (r < 12) { // 12% chance to drop something
    int t = rand()%100;
    if (t < 40) items.emplace_back(x,y, IT_HEALTH);
    else if (t < 65) items.emplace_back(x,y, IT_RAPID);
    else if (t < 85) items.emplace_back(x,y, IT_DAMAGE);
    else items.emplace_back(x,y, IT_SHIELD);
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
      // choose bullet char by player.type
      char bch = '|';
      if (player.type == "Standard") bch = '|';
      else if (player.type == "Heavy") bch = '#';
      else if (player.type == "Light") bch = ':';
      else if (player.type == "Sniper") bch = '-';
      else if (player.type == "RapidFire") bch = '!';
      else if (player.type == "Plasma") bch = '*';

      // spawn bullets according to shotCount
      for (int s=0; s<player.shotCount; ++s) {
        int ox = 0;
        if (player.shotCount == 1) ox = 0;
        else if (player.shotCount == 2) ox = (s==0)?-1:1;
        else ox = s-1; // -1,0,1
        bullets.emplace_back(player.x+ox, player.y-4, -1, player.shotDamage, bch);
      }

      // apply rapid fire if active (shorten cooldown)
      int baseFR = player.fireRate;
      if (rapidFireTimer > 0) baseFR = max(1, player.fireRate/2);
      shootCooldown = baseFR;
    }
    else if (c == 'q') running = false;
    clampPos(player.x, player.y);
  }
}

void updateGameLogic() {
  tickCount++;

  // decrease power-up timers
  if (rapidFireTimer > 0) rapidFireTimer--;
  if (damageBoostTimer > 0) damageBoostTimer--;

  // move bullets
  for (auto &b: bullets) b.y += b.dy;
  bullets.erase(remove_if(bullets.begin(), bullets.end(),
    [](const Bullet &b){ return b.y < 1 || b.y >= HEIGHT-1; }), bullets.end());

  // move bombs (boss bombs falling)
  for (auto &bm: bombs) bm.y += bm.dy;
  bombs.erase(remove_if(bombs.begin(), bombs.end(),
    [](const Bomb &b){ return b.y >= HEIGHT-1; }), bombs.end());

  // global delay to slow enemies slightly
  int globalDelay = max(2, 10 - level/2);

  for (auto &e : enemies) {
    bool doMove = false;
    if (e.type == FAST) {
      int d = max(1, globalDelay/2);
      doMove = (tickCount % d) == 0;
    } else if (e.type == BOSS) {
      // boss moves less frequently
      doMove = (tickCount % 4) == 0;
    } else {
      doMove = (tickCount % globalDelay) == 0;
    }
    if (!doMove) continue;

    switch (e.type) {
      case FAST: e.y += 1; break;
      case STRONG: e.y += 1; break;
      case NORMAL: e.y += 1; break;
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
        int dx = player.x - e.x;
        int dy = player.y - e.y;
        if (abs(dx) <= 20 && abs(dy) <= 10) {
          e.x += (dx==0?0: (dx>0?1:-1));
          e.y += (dy==0?0: (dy>0?1:-1));
        } else {
          e.y += 1;
        }
        break;
      }
      case BOSS: {
        // boss moves horizontally and occasionally uses skills
        e.x += e.dir;
        if (e.x <= 3 || e.x >= WIDTH-4) e.dir *= -1;

        // boss skill cooldown handling
        if (e.skillCooldown > 0) e.skillCooldown--;
        if (e.skillCooldown <= 0) {
          // choose skill
          int r = rand()%100;
          bool strongPhase = (e.hp <= ( (20 + level*5) / 2 ));
          if (r < 45) {
            // laser
            laser.active = true;
            laser.y = e.y + 2; // sweep a row below boss
            laser.life = strongPhase ? 10 : 6;
          } else {
            // bomb rain: spawn several bombs below boss
            int count = strongPhase ? 8 : 5;
            for (int b=0;b<count;b++) {
              int bx = max(2, min(WIDTH-3, e.x -2 + (rand()%7)));
              bombs.emplace_back(bx, e.y+2, 1);
            }
          }
          // reset cooldown (shorter in strong phase)
          e.skillCooldown = strongPhase ? 30 : 50;
        }
        break;
      }
    }
  }

  // spawn
  if (tickCount % max(8, enemySpawnRate - level*3) == 0)
    spawnEnemiesByLevel();

  // collisions: bullets vs enemies and boss interactions
  vector<int> rmB, rmE;
  for (int i=0;i<(int)bullets.size();++i) {
    for (int j=0;j<(int)enemies.size();++j) {
      if (abs(bullets[i].x - enemies[j].x) <= 1 && abs(bullets[i].y - enemies[j].y) <= 1) {
        rmB.push_back(i);
        enemies[j].hp -= bullets[i].dmg + (damageBoostTimer>0 ? 1 : 0);
        explosions.emplace_back(bullets[i].x, bullets[i].y, EXPLOSION_FRAMES/2);
        if (enemies[j].hp <= 0) {
          // drop item maybe
          maybeDropItem(enemies[j].x, enemies[j].y);
          rmE.push_back(j);
          score += 10;
          explosions.emplace_back(enemies[j].x, enemies[j].y, EXPLOSION_FRAMES);
        }
      }
    }
  }

  // remove bullets & enemies (reverse order)
  sort(rmB.rbegin(), rmB.rend());
  sort(rmE.rbegin(), rmE.rend());
  for (int i: rmB) if (i < (int)bullets.size()) bullets.erase(bullets.begin()+i);
  for (int j: rmE) if (j < (int)enemies.size()) enemies.erase(enemies.begin()+j);

  // handle bombs hitting player or ground
  for (int bi = (int)bombs.size()-1; bi>=0; --bi) {
    Bomb &bm = bombs[bi];
    // if bomb hits player
    if (abs(bm.x - player.x) <= 0 && abs(bm.y - player.y) <= 1) {
      if (player.shieldCount > 0) {
        player.shieldCount--;
        explosions.emplace_back(player.x, player.y, EXPLOSION_FRAMES);
      } else {
        explosions.emplace_back(player.x, player.y, EXPLOSION_FRAMES);
        running = false;
        return;
      }
      bombs.erase(bombs.begin()+bi);
      continue;
    }
    // if reaches bottom, just explode
    if (bm.y >= HEIGHT-3) {
      explosions.emplace_back(bm.x, bm.y, 2);
      bombs.erase(bombs.begin()+bi);
      continue;
    }
  }

  // items pickup
  for (int ii = (int)items.size()-1; ii>=0; --ii) {
    Item &it = items[ii];
    // decrement life
    it.life--;
    if (it.life <= 0) { items.erase(items.begin()+ii); continue; }
    // pickup if player overlaps
    if (abs(it.x - player.x) <= 1 && abs(it.y - player.y) <= 1) {
      if (it.t == IT_HEALTH) {
        player.hp = min(player.hp + 1, 12);
      } else if (it.t == IT_SHIELD) {
        player.shieldCount++;
      } else if (it.t == IT_RAPID) {
        rapidFireTimer = 600; // e.g. 600 ticks ~ 24s at 40ms/frame
      } else if (it.t == IT_DAMAGE) {
        damageBoostTimer = 600;
      }
      items.erase(items.begin()+ii);
    }
  }

  // explosions update
  for (auto &ex: explosions) ex.life--;
  explosions.erase(remove_if(explosions.begin(), explosions.end(),
    [](const Explosion &e){ return e.life <= 0; }), explosions.end());

  // laser active effects - damage player if on laser row
  if (laser.active) {
    if (laser.life > 0) {
      // if player in row, damage
      if (abs(player.y - laser.y) <= 0) {
        if (player.shieldCount > 0) {
          player.shieldCount--;
          explosions.emplace_back(player.x, player.y, EXPLOSION_FRAMES/2);
        } else {
          explosions.emplace_back(player.x, player.y, EXPLOSION_FRAMES);
          running = false;
          return;
        }
      }
      laser.life--;
    } else {
      laser.active = false;
    }
  }

  // player collision with enemies (immediate end or shield)
  for (int idx = (int)enemies.size()-1; idx >= 0; --idx) {
    Enemy &e = enemies[idx];
    int thresh = (e.type==STRONG)?3:2;
    if (e.type==BOSS) thresh = 4;
    if (abs(e.x - player.x) <= thresh && abs(e.y - player.y) <= thresh) {
      if (player.shieldCount > 0) {
        player.shieldCount--;
        explosions.emplace_back(e.x, e.y, EXPLOSION_FRAMES);
        explosions.emplace_back(player.x, player.y, EXPLOSION_FRAMES/2);
        enemies.erase(enemies.begin() + idx);
        score += 5;
        continue;
      } else {
        explosions.emplace_back(e.x, e.y, EXPLOSION_FRAMES);
        explosions.emplace_back(player.x, player.y, EXPLOSION_FRAMES);
        running = false;
        return;
      }
    }
  }

  // remove enemies that passed bottom
  enemies.erase(remove_if(enemies.begin(), enemies.end(),
    [](const Enemy &e){ return e.y >= HEIGHT-3; }), enemies.end());

  // level up
  if (score >= level * 200) {
    level++;
    player.hp = min(player.hp + 1, 12);
    for (int i=0;i<2;i++) spawnEnemiesByLevel();
    // spawn boss on medium/hard handled elsewhere; here spawn occasional boss
    if (level % 3 == 0) spawnBoss();
  }
}

// ---------- Rendering ----------
string buildOutputBuffer(const vector<string> &scr) {
  // Count enemy types for HUD
  int cntNormal=0,cntFast=0,cntStrong=0,cntBouncer=0,cntZig=0,cntChaser=0,cntBoss=0;
  for (auto &e: enemies) {
    switch (e.type) {
      case NORMAL: cntNormal++; break;
      case FAST: cntFast++; break;
      case STRONG: cntStrong++; break;
      case BOUNCER: cntBouncer++; break;
      case ZIGZAG: cntZig++; break;
      case CHASER: cntChaser++; break;
      case BOSS: cntBoss++; break;
    }
  }

  string out;
  out.reserve(WIDTH * HEIGHT + 512);
  for (int y=0;y<HEIGHT;y++) {
    for (int x=0;x<WIDTH;x++) {
      char ch = scr[y][x];
      if (ch == '*') {
        if ((tickCount/2)%2==0) out += COL_EXP1 + "*" + colorReset();
        else out += COL_EXP2 + "*" + colorReset();
      }
      else if (ch == '#') out += fgColor(91) + "#" + colorReset();
      else if (ch == '/' || ch == '\\' || ch == '_' || ch == '^') out += fgColor(95) + string(1,ch) + colorReset();
      else if (ch == '+' || ch == '-' || ch == '+' || ch == '+') out += fgColor(34) + string(1,ch) + colorReset();
      else if (ch == '=') out += fgColor(93) + "=" + colorReset();
      else if (ch == '&') out += fgColor(93) + "&" + colorReset();
      else if (ch == 'Z') out += fgColor(96) + "Z" + colorReset();
      else if (ch == 'C') out += fgColor(95) + "C" + colorReset();
      else if (ch == '+' || ch == '+') out += fgColor(35) + string(1,ch) + colorReset();
      else if (ch == 'o') out += fgColor(91) + "o" + colorReset();
      else if (ch == 'S' || ch == '+' || ch == 'R' || ch == 'D') out += fgColor(93) + string(1,ch) + colorReset();
      else if (ch == '|' ) out += COL_BULLET + "|" + colorReset();
      else if (ch == '-' ) out += fgColor(95) + "-" + colorReset();
      else if (ch == ':' ) out += fgColor(97) + ":" + colorReset();
      else if (ch == '!' ) out += fgColor(93) + "!" + colorReset();
      else if (ch == 'O' || ch == 'o') out += fgColor(36) + string(1,ch) + colorReset();
      else if (ch == 'O') out += fgColor(36) + "O" + colorReset();
      else if (ch == '|' || ch == '-') out += COL_BORDER + ch + colorReset();
      else out.push_back(ch);
    }
    out.push_back('\n');
  }

  // HUD line: power-ups / timers
  string active = "";
  if (rapidFireTimer > 0) {
    active += "RapidFire(" + to_string(rapidFireTimer/25) + "s) ";
  }
  if (damageBoostTimer > 0) {
    active += "Damage++(" + to_string(damageBoostTimer/25) + "s) ";
  }
  if (player.shieldCount > 0) {
    active += "Shield:" + to_string(player.shieldCount) + " ";
  }

  out += COL_TEXT + string(" Tank: ") + player.type +
         " | Score: " + to_string(score) +
         " | HP: " + to_string(player.hp) +
         " | " + (active.empty()? "No PowerUps": active) +
         " | Level: " + to_string(level) +
         " | Enemies: " + to_string((int)enemies.size())
         + " (N:" + to_string(cntNormal)
         + " F:" + to_string(cntFast)
         + " S:" + to_string(cntStrong)
         + " B:" + to_string(cntBouncer)
         + " Z:" + to_string(cntZig)
         + " C:" + to_string(cntChaser)
         + " Boss:" + to_string(cntBoss)
         + ")   (W/A/S/D move, Space shoot, Q quit)"
         + colorReset() + "\n";

  return out;
}

void renderScreen() {
  auto scr = createEmptyScreen();
  drawBorder(scr);
  // draw items, bombs, laser first so they appear behind explosions/tank if overlap
  drawItems(scr);
  drawBombs(scr);
  drawLaser(scr);
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
  cout << "1. Standard    - HP:5  Speed:1  FireRate:6  (Balanced)\n";
  cout << "2. Heavy       - HP:8  Speed:1  FireRate:8  (Armored)\n";
  cout << "3. Light       - HP:3  Speed:2  FireRate:4  (Agile)\n";
  cout << "4. Sniper      - HP:4  Speed:1  FireRate:9  (High damage, narrow)\n";
  cout << "5. RapidFire   - HP:4  Speed:1  FireRate:2  (Very fast shooting)\n";
  cout << "6. Plasma      - HP:6  Speed:1  FireRate:5  (Energy shots)\n\n";
  cout << "Choose 1..6: " << colorReset();
  int choice = 1;
  while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
  choice = kb_get();
  if (choice >= '1' && choice <= '6') return choice - '0';
  return 1;
}

void showTitleScreen() {
  cout << "\x1B[2J\x1B[H" << COL_TEXT;
  cout << "=====================================\n";
  cout << "        TANK SHOOTER - v5.0          \n";
  cout << "    (PowerUps & Boss Skills added)   \n";
  cout << "=====================================\n\n";
  cout << "1. Start Game\n2. Instructions\n3. Tank & Enemy Info\n4. Quit\n\n";
  cout << colorReset() << "Choose an option: ";
}

void showInstructions() {
  cout << "\x1B[2J\x1B[H" << COL_TEXT;
  cout << "Instructions:\n";
  cout << "- Move with W/A/S/D.\n";
  cout << "- Shoot with Space.\n";
  cout << "- Avoid or destroy enemies before they hit you.\n";
  cout << "- PowerUps drop from enemies sometimes (+ S R D)\n";
  cout << "- Boss uses Laser (horizontal) and Bomb Rain.\n\n";
  cout << "Press any key to return.\n" << colorReset();
  while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
  kb_get();
}

void showInfoScreen() {
    cout << "\x1B[2J\x1B[H" << COL_TEXT;
    cout << "==============================\n";
    cout << "     TANK & ENEMY INFO       \n";
    cout << "==============================\n\n";

    cout << fgColor(93) << "TANK TYPES:\n" << colorReset();
    cout << COL_TANK << "Standard " << colorReset() << "- '*' shape, balanced stats.\n";
    cout << fgColor(91) << "Heavy     " << colorReset() << "- '#' tank, strong armor, slow speed.\n";
    cout << fgColor(96) << "Light     " << colorReset() << "- '+' agile but fragile.\n";
    cout << fgColor(95) << "Sniper    " << colorReset() << "- '^' narrow gun, high damage.\n";
    cout << fgColor(93) << "RapidFire " << colorReset() << "- '=' barrel, fires very fast.\n";
    cout << fgColor(35) << "Plasma    " << colorReset() << "- 'O' style, energy bullets.\n\n";

    cout << fgColor(93) << "ENEMY TYPES:\n" << colorReset();
    cout << fgColor(91) << "# Normal   " << colorReset() << "- Basic slow-moving target.\n";
    cout << fgColor(95) << "/^\\ Fast   " << colorReset() << "- Moves quickly, low HP.\n";
    cout << fgColor(34) << "+-+ Strong " << colorReset() << "- Tough armor, slower.\n";
    cout << fgColor(93) << "& Bouncer  " << colorReset() << "- Moves side-to-side as it descends.\n";
    cout << fgColor(96) << "Z Zigzag   " << colorReset() << "- Erratic movement pattern.\n";
    cout << fgColor(95) << "C Chaser   " << colorReset() << "- Tracks and hunts player.\n";
    cout << fgColor(35) << "BOSS       " << colorReset() << "- Large tank with Laser and Bomb skills.\n\n";

    cout << fgColor(97) << "PowerUps (drop): + Health, S Shield, R RapidFire, D DamageBoost\n\n" << colorReset();
    cout << "Press any key to return to menu..." << endl;
    while (!kb_hit()) this_thread::sleep_for(chrono::milliseconds(50));
    kb_get();
}

// ---------- Game loop ----------
void runGameLoop() {
  bullets.clear(); enemies.clear(); explosions.clear(); items.clear(); bombs.clear();
  score = 0; tickCount = 0; level = 1; enemySpawnRate = START_ENEMY_RATE;
  running = true;
  rapidFireTimer = 0; damageBoostTimer = 0;

  int choice = chooseTank();
  if (choice == 1) player = Tank(WIDTH/2, HEIGHT-4, 5, "Standard", 1, 6, 1, 1, 0);
  else if (choice == 2) player = Tank(WIDTH/2, HEIGHT-4, 8, "Heavy", 1, 8, 1, 1, 0);
  else if (choice == 3) player = Tank(WIDTH/2, HEIGHT-4, 3, "Light", 2, 4, 1, 1, 0);
  else if (choice == 4) player = Tank(WIDTH/2, HEIGHT-4, 4, "Sniper", 1, 9, 2, 1, 0);
  else if (choice == 5) player = Tank(WIDTH/2, HEIGHT-4, 4, "RapidFire", 1, 2, 1, 1, 0);
  else player = Tank(WIDTH/2, HEIGHT-4, 6, "Plasma", 1, 5, 1, 1, 0);

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
  cout << "\n?? GAME OVER ??\n\n";
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
    else if (opt == '3') showInfoScreen();
    else break;
  }

  kb_restore();
  restoreConsole();
  cout << colorReset() << "\nGoodbye!\n";
  return 0;
}

