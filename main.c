#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("CoD_PSP_Ops", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BUF_WIDTH  512
#define SCR_WIDTH  480
#define SCR_HEIGHT 272
#define PI 3.14159265f

static unsigned int __attribute__((aligned(16))) list[262144];

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

// Askeri Tesis ve Siper Modelleri icin Vertex Verileri
Vertex __attribute__((aligned(16))) wallVertices[36] = {
    {0xFF2E3B2E, -0.5f, -0.5f,  0.5f}, {0xFF2E3B2E,  0.5f, -0.5f,  0.5f}, {0xFF4A5D4A,  0.5f,  0.5f,  0.5f},
    {0xFF2E3B2E, -0.5f, -0.5f,  0.5f}, {0xFF4A5D4A,  0.5f,  0.5f,  0.5f}, {0xFF4A5D4A, -0.5f,  0.5f,  0.5f},
    {0xFF1F2B1F,  0.5f, -0.5f, -0.5f}, {0xFF1F2B1F, -0.5f, -0.5f, -0.5f}, {0xFF3A4B3A, -0.5f,  0.5f, -0.5f},
    {0xFF1F2B1F,  0.5f, -0.5f, -0.5f}, {0xFF3A4B3A, -0.5f,  0.5f, -0.5f}, {0xFF3A4B3A,  0.5f,  0.5f, -0.5f},
    {0xFF5A6D5A, -0.5f,  0.5f,  0.5f}, {0xFF5A6D5A,  0.5f,  0.5f,  0.5f}, {0xFF5A6D5A,  0.5f,  0.5f, -0.5f},
    {0xFF5A6D5A, -0.5f,  0.5f,  0.5f}, {0xFF5A6D5A,  0.5f,  0.5f, -0.5f}, {0xFF5A6D5A, -0.5f,  0.5f, -0.5f},
    {0xFF151C15, -0.5f, -0.5f, -0.5f}, {0xFF151C15,  0.5f, -0.5f, -0.5f}, {0xFF151C15,  0.5f, -0.5f,  0.5f},
    {0xFF151C15, -0.5f, -0.5f, -0.5f}, {0xFF151C15,  0.5f, -0.5f,  0.5f}, {0xFF151C15, -0.5f, -0.5f,  0.5f},
    {0xFF3A4B3A, -0.5f, -0.5f, -0.5f}, {0xFF3A4B3A, -0.5f, -0.5f,  0.5f}, {0xFF4A5D4A, -0.5f,  0.5f,  0.5f},
    {0xFF3A4B3A, -0.5f, -0.5f, -0.5f}, {0xFF4A5D4A, -0.5f,  0.5f,  0.5f}, {0xFF4A5D4A, -0.5f,  0.5f, -0.5f},
    {0xFF3A4B3A,  0.5f, -0.5f,  0.5f}, {0xFF3A4B3A,  0.5f, -0.5f, -0.5f}, {0xFF4A5D4A,  0.5f, -0.5f, -0.5f},
    {0xFF3A4B3A,  0.5f, -0.5f,  0.5f}, {0xFF4A5D4A,  0.5f, -0.5f, -0.5f}, {0xFF4A5D4A,  0.5f,  0.5f,  0.5f}
};

Vertex __attribute__((aligned(16))) enemyVertices[36];
Vertex __attribute__((aligned(16))) gunVertices[36];

#define GRID_LINES 44
Vertex __attribute__((aligned(16))) gridVertices[GRID_LINES * 2];

typedef struct {
    float x, z;
    float angle;        // Govde yonu
    float pitch;        // Kamera dik acisi (CoD tarzi bakis)
    int hp, maxHp;
    int ammo;
    int reserveAmmo;
    int score;
    int wave;
    int isAiming;       // R (Right Trigger) ile ADS (Nişan Alma)
} PlayerState;

typedef struct {
    int active;
    float x, z;
    float angle;
    int hp;
    int shootTimer;
} Enemy;

typedef struct {
    int active;
    int fromPlayer;
    float x, y, z;
    float vx, vy, vz;
    int dmg;
    int life;
} Bullet;

#define MAX_ENEMIES 10
#define MAX_BULLETS 30
#define MAX_OBSTACLES 20

PlayerState player;
Enemy enemies[MAX_ENEMIES];
Bullet bullets[MAX_BULLETS];
float obsX[MAX_OBSTACLES], obsZ[MAX_OBSTACLES];

unsigned int oldButtons = 0;

int exit_callback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
    return 0;
}

int CallbackThread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

void SetupCallbacks(void) {
    int thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

void InitModels(void) {
    for (int i = 0; i < 36; i++) {
        // Dusman askerleri (Koyu zeytin yesili / kamuflaj)
        enemyVertices[i] = wallVertices[i];
        unsigned int c = wallVertices[i].color;
        unsigned int r = (c >> 16) & 0xFF;
        enemyVertices[i].color = 0xFF152035 | (r << 16); // Taktiksel mavi-gri ton

        // Silah modeli (Koyu metalik siyah)
        gunVertices[i] = wallVertices[i];
        gunVertices[i].color = 0xFF252525;
    }

    int idx = 0;
    for (int i = -10; i <= 10; i++) {
        float pos = i * 8.0f;
        gridVertices[idx++] = (Vertex){0xFF223022, pos, 0.0f, -80.0f};
        gridVertices[idx++] = (Vertex){0xFF223022, pos, 0.0f,  80.0f};
        gridVertices[idx++] = (Vertex){0xFF223022, -80.0f, 0.0f, pos};
        gridVertices[idx++] = (Vertex){0xFF223022,  80.0f, 0.0f, pos};
    }

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        float a = (float)i * 0.9f;
        float d = 16.0f + (i % 6) * 10.0f;
        obsX[i] = cosf(a) * d;
        obsZ[i] = sinf(a) * d;
    }
}

void SpawnWave(int wave) {
    int count = 3 + wave * 2;
    if (count > MAX_ENEMIES) count = MAX_ENEMIES;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (i < count) {
            float a = (2.0f * PI * i) / count;
            float dist = 30.0f + (i % 4) * 8.0f;
            enemies[i].active = 1;
            enemies[i].x = player.x + sinf(a) * dist;
            enemies[i].z = player.z + cosf(a) * dist;
            enemies[i].hp = 50 + wave * 15;
            enemies[i].shootTimer = 50 + i * 20;
        } else {
            enemies[i].active = 0;
        }
    }
}

void InitGame(void) {
    memset(&player, 0, sizeof(PlayerState));
    player.hp = 100;
    player.maxHp = 100;
    player.ammo = 30;
    player.reserveAmmo = 90;
    player.wave = 1;
    player.angle = 0.0f;
    player.pitch = 0.0f;

    memset(bullets, 0, sizeof(bullets));
    SpawnWave(1);
}

void FireBullet(int fromPlayer, float x, float y, float z, float angle, float pitch, int dmg, float speed) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].active = 1;
            bullets[i].fromPlayer = fromPlayer;
            bullets[i].x = x;
            bullets[i].y = y;
            bullets[i].z = z;
            bullets[i].vx = sinf(angle) * cosf(pitch) * speed;
            bullets[i].vy = sinf(pitch) * speed;
            bullets[i].vz = cosf(angle) * cosf(pitch) * speed;
            bullets[i].dmg = dmg;
            bullets[i].life = 70;
            break;
        }
    }
}

void DrawBox(float x, float y, float z, float sx, float sy, float sz, float rotY, Vertex* verts) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 pos = {x, y, z};
    ScePspFVector3 scl = {sx, sy, sz};
    sceGumTranslate(&pos);
    sceGumRotateY(rotY);
    sceGumScale(&scl);
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, verts);
}

void UpdateGame(SceCtrlData* pad) {
    unsigned int pressed = pad->Buttons & ~oldButtons;
    oldButtons = pad->Buttons;

    // CoD Tarzi Kontroller:
    // Sol Analog Stick: İleri/Geri hareket ve Yana strafe (CoD PSP yürüyüş mekaniği)
    // Sağ Analog / D-Pad: Kamera döndürme (Nişan alma)
    // R (Right Trigger): ADS (Nişan Modu - Demir arpacık bakışı)
    // L (Left Trigger): El Bombası / Taktiksel Odak
    // X (Cross): Ateş Et
    // Kare (Square): Şarjör Değiştir (Reload)
    // Üçgen (Triangle): Zıplama / Hızlı Dönüş

    player.isAiming = (pad->Buttons & PSP_CTRL_RTRIGGER) ? 1 : 0;

    float ax = ((float)pad->Lx - 128.0f) / 128.0f;
    float ay = ((float)pad->Ly - 128.0f) / 128.0f;
    if (fabsf(ax) < 0.18f) ax = 0.0f;
    if (fabsf(ay) < 0.18f) ay = 0.0f;

    // D-Pad ile hassas kamera kontrolü (CoD PSP tuş kombinasyonu)
    if (pad->Buttons & PSP_CTRL_LEFT) player.angle += 0.045f;
    if (pad->Buttons & PSP_CTRL_RIGHT) player.angle -= 0.045f;
    if (pad->Buttons & PSP_CTRL_UP) {
        player.pitch += 0.035f;
        if (player.pitch > 0.6f) player.pitch = 0.6f;
    }
    if (pad->Buttons & PSP_CTRL_DOWN) {
        player.pitch -= 0.035f;
        if (player.pitch < -0.6f) player.pitch = -0.6f;
    }

    float moveSpd = player.isAiming ? 0.08f : 0.15f; // Nişan alırken yavaş yürü (CoD gerçekçiliği)
    player.x += (sinf(player.angle) * (-ay) + cosf(player.angle) * ax) * moveSpd;
    player.z += (cosf(player.angle) * (-ay) - sinf(player.angle) * ax) * moveSpd;

    // Şarjör Değiştirme (Kare Tuşu)
    if ((pressed & PSP_CTRL_SQUARE) && player.reserveAmmo > 0 && player.ammo < 30) {
        int needed = 30 - player.ammo;
        if (player.reserveAmmo >= needed) {
            player.ammo = 30;
            player.reserveAmmo -= needed;
        } else {
            player.ammo += player.reserveAmmo;
            player.reserveAmmo = 0;
        }
    }

    // Ateş Etme (X Tuşu)
    if ((pressed & PSP_CTRL_CROSS) && player.ammo > 0) {
        player.ammo--;
        float spread = player.isAiming ? 0.01f : 0.05f; // ADS modunda nokta atışı
        float finalAngle = player.angle + ((float)(rand() % 100) - 50.0f) * 0.001f * spread;
        float finalPitch = player.pitch + ((float)(rand() % 100) - 50.0f) * 0.001f * spread;
        FireBullet(1, player.x, 1.2f, player.z, finalAngle, finalPitch, player.isAiming ? 35 : 25, 1.8f);
    }

    // Düşman Yapay Zekası
    int activeCount = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        activeCount++;

        float dx = player.x - enemies[i].x;
        float dz = player.z - enemies[i].z;
        float dist = sqrtf(dx * dx + dz * dz);
        float targetAngle = atan2f(dx, dz);
        enemies[i].angle = targetAngle;

        if (dist > 10.0f) {
            enemies[i].x += sinf(targetAngle) * 0.09f;
            enemies[i].z += cosf(targetAngle) * 0.09f;
        }

        enemies[i].shootTimer--;
        if (enemies[i].shootTimer <= 0 && dist < 45.0f) {
            FireBullet(0, enemies[i].x, 1.2f, enemies[i].z, targetAngle, 0.0f, 12, 1.2f);
            enemies[i].shootTimer = 70 + (rand() % 40);
        }
    }

    if (activeCount == 0) {
        player.wave++;
        SpawnWave(player.wave);
        player.reserveAmmo += 60;
    }

    // Mermi Fizik Güncellemesi
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        bullets[i].x += bullets[i].vx;
        bullets[i].y += bullets[i].vy;
        bullets[i].z += bullets[i].vz;
        bullets[i].life--;
        if (bullets[i].life <= 0) {
            bullets[i].active = 0;
            continue;
        }

        if (bullets[i].fromPlayer) {
            for (int j = 0; j < MAX_ENEMIES; j++) {
                if (!enemies[j].active) continue;
                float dx = bullets[i].x - enemies[j].x;
                float dz = bullets[i].z - enemies[j].z;
                if (dx * dx + dz * dz < 2.2f) {
                    enemies[j].hp -= bullets[i].dmg;
                    bullets[i].active = 0;
                    if (enemies[j].hp <= 0) {
                        enemies[j].active = 0;
                        player.score += 150;
                    }
                    break;
                }
            }
        } else {
            float dx = bullets[i].x - player.x;
            float dz = bullets[i].z - player.z;
            if (dx * dx + dz * dz < 2.0f) {
                player.hp -= bullets[i].dmg;
                bullets[i].active = 0;
                if (player.hp <= 0) {
                    player.hp = player.maxHp;
                    player.x = 0.0f;
                    player.z = 0.0f;
                }
            }
        }
    }
}

int main(void) {
    SetupCallbacks();
    pspDebugScreenInit();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    InitModels();
    InitGame();

    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void*)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CCW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    SceCtrlData pad;

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        UpdateGame(&pad);

        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xFF0F141C); // Call of Duty tarzı karanlık taktiksel ton
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        // CoD tarzı dinamik FOV (R tuşuna basınca ADS zoom efekti)
        float fov = player.isAiming ? 35.0f : 65.0f;
        sceGumPerspective(fov, 16.0f / 9.0f, 0.2f, 150.0f);

        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();
        
        // FPS Kamera (Göz hizası ve silah sarsıntısı)
        ScePspFVector3 eye = {player.x, 1.4f, player.z};
        ScePspFVector3 center = {
            player.x + sinf(player.angle) * cosf(player.pitch) * 10.0f,
            1.4f + sinf(player.pitch) * 10.0f,
            player.z + cosf(player.angle) * cosf(player.pitch) * 10.0f
        };
        ScePspFVector3 up = {0.0f, 1.0f, 0.0f};
        sceGumLookAt(&eye, &center, &up);

        // Zemin Grid
        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();
        sceGumDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, GRID_LINES * 2, 0, gridVertices);

        // Siperler ve Binalar
        for (int i = 0; i < MAX_OBSTACLES; i++) {
            DrawBox(obsX[i], 1.0f, obsZ[i], 2.5f, 2.0f, 2.5f, 0.0f, wallVertices);
        }

        // Düşman Askerleri
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active) {
                DrawBox(enemies[i].x, 1.0f, enemies[i].z, 1.2f, 2.2f, 1.2f, enemies[i].angle, enemyVertices);
            }
        }

        // Mermiler
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bullets[i].active) {
                DrawBox(bullets[i].x, bullets[i].y, bullets[i].z, 0.2f, 0.2f, 0.4f, 0.0f, enemyVertices);
            }
        }

        // Ekranın Altında Sabit Duran First-Person Silah Modeli (CoD Elle Tutulan Silah Hissiyatı)
        if (!player.isAiming) {
            DrawBox(player.x + sinf(player.angle + 1.57f) * 0.35f + sinf(player.angle) * 0.8f,
                    1.0f,
                    player.z + cosf(player.angle + 1.57f) * 0.35f + cosf(player.angle) * 0.8f,
                    0.25f, 0.3f, 1.1f, player.angle, gunVertices);
        }

        sceGuFinish();
        sceGuSync(0, 0);

        // CoD Tarzı HUD / Arayüz
        pspDebugScreenSetXY(1, 1);
        pspDebugScreenPrintf("CALL OF DUTY: PSP OPS | DALGA:%d | CAN:%d/100", player.wave, player.hp);
        pspDebugScreenSetXY(1, 2);
        pspDebugScreenPrintf("MERMI: %02d / %02d  [SKOR: %d] %s", player.ammo, player.reserveAmmo, player.score, player.isAiming ? "[ADS - NISAN ALINIYOR]" : "");

        pspDebugScreenSetXY(1, 32);
        pspDebugScreenPrintf("[Analog] Hareket | [D-Pad] Nisandaki Yon | [R] ADS Nisan | [X] Ates | [Kare] Sarjor");

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    return 0;
}
