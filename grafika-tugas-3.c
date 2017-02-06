#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>

typedef struct Points {
    int x;
    int y;
} Point;
typedef struct Colors {
    char a;
    char r;
    char g;
    char b;
} Color;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

Color bg; // warna background
char* fbp; // memory map ke fb0
int fbfd; // file fb0
int screensize; // length nya si fbp
int kaboom = 0; // bernilai 1 jika pesawat sudah tertembak
int overPixel = 10; // intinya satu baris ada 1376 pixel (1366 pixel + 10 pixel "ngumpet")
int bytePerPixel; // 4
int srcXBeam, srcYBeam, destXBeam; // peluru bergerak dari (srcXBeam,srcYBeam) ke (destXBeam,0)
int headPlane, tailPlane; // titik koordinat terdepan dan terbelakang pesawat

void setPoint(Point* p, int x, int y) {
    p -> x = x;
    p -> y = y;
}

void setColor(Color* c, char r, char g, char b) {
    c -> a = 0;
    c -> r = r;
    c -> g = g;
    c -> b = b;
}

void getColor(Point* p, Color* c) {
    // mengambil warna pixel layar pada titik p
    int location = (p -> x + vinfo.xoffset) * bytePerPixel + (p -> y + vinfo.yoffset) * finfo.line_length;
    c -> b = *(fbp + location);
    c -> g = *(fbp + location + 1);
    c -> r = *(fbp + location + 2);
    c -> a = *(fbp + location + 3);
}

int isColorSame(Color* c1, Color* c2) {
    if ((c1 -> b == c2 -> b) && (c1 -> g == c2 -> g) &&
        (c1 -> r == c2 -> r) && (c1 -> a == c2 -> a)) {
        return 1;
    } else {
        return 0;
    }
}

void drawLocation(int location, Color* c) {
    // mengatur warna pixel layar berdasarkan index array frame buffer
    *(fbp + location) = c -> b;
    *(fbp + location + 1) = c -> g;
    *(fbp + location + 2) = c -> r;
    *(fbp + location + 3) = c -> a;
}

void drawPoint(Point* p, Color *c) {
    // mengatur warna pixel layar pada titik p
    int location = (p -> x + vinfo.xoffset) * bytePerPixel + (p -> y + vinfo.yoffset) * finfo.line_length;
    drawLocation(location, c);
}

void clearScreen(Color* c) {
    int i, j;
    int location = 0;
    // iterasi pixel setiap baris dan setiap kolom
    for (j = 0; j < vinfo.yres; j++) {
        for (i = 0; i < vinfo.xres; i++) {
            drawLocation(location, c);
            location += bytePerPixel; // pixel sebelah kanannya berjarak sekian byte
        }
        location += (bytePerPixel * overPixel); // pixel pertama pada baris selanjutnya berjarak sekian byte
    }
}

void drawLineX(Point* p1, Point* p2, Color* c, int positif) {
    // algoritma Bresenham untuk delta X yang lebih panjang
    int dx = p2 -> x - p1 -> x;
    int dy = (p2 -> y - p1 -> y) * positif;
    int d = dy + dy - dx;
    int j = p1 -> y;
    int i, location;
    
    for (i = p1 -> x; i <= p2 -> x; i++) {
        location = (i + vinfo.xoffset) * bytePerPixel + (j + vinfo.yoffset) * finfo.line_length;
        drawLocation(location, c);
        if (d > 0) {
            // positif = 1 berarti y makin lama makin bertambah
            j += positif;
            d -= dx;
        }
        d += dy;
    }
}

void drawLineY(Point* p1, Point* p2, Color* c, int positif) {
    // algoritma Bresenham untuk delta Y yang lebih panjang
    int dx = (p2 -> x - p1 -> x) * positif;
    int dy = p2 -> y - p1 -> y;
    int d = dx + dx - dy;
    int i = p1 -> x;
    int j, location;
    
    for (j = p1 -> y; j <= p2 -> y; j++) {
        location = (i + vinfo.xoffset) * bytePerPixel + (j + vinfo.yoffset) * finfo.line_length;
        drawLocation(location, c);
        if (d > 0) {
            // positif = 1 berarti x makin lama makin bertambah
            i += positif;
            d -= dy;
        }
        d += dx;
    }
}

void drawLine(Point* p1, Point* p2, Color* c) {
    // algoritma bresenham sebenernya hanya bisa menggambar di octant 0,
    // atur sedemikian rupa supaya 7 octant lainnya bisa masuk ke algoritma
    int dx = p2 -> x - p1 -> x;
    int dy = p2 -> y - p1 -> y;
    /*  peta octant:
        \2|1/
        3\|/0
        --+--
        4/|\7
        /5|6\
    */    
    if (dy >= 0 && dy < dx) { // octant 0
        drawLineX(p1, p2, c, 1);
    } else if (dx > 0 && dx <= dy) { // octant 1
        drawLineY(p1, p2, c, 1);
    } else if (dx <= 0 && dx > -dy) { // octant 2
        drawLineY(p1, p2, c, -1);
    } else if (dy > 0 && dy <= -dx) { // octant 3
        drawLineX(p2, p1, c, -1);
    } else if (dy <= 0 && dy > dx) { // octant 4
        drawLineX(p2, p1, c, 1);
    } else if (dx < 0 && dx >= dy) { // octant 5
        drawLineY(p2, p1, c, 1);
    } else if (dx >= 0 && dx < -dy) { // octant 6
        drawLineY(p2, p1, c, -1);
    } else { // dy < 0 && dy >= -dx // octant 7
        drawLineX(p1, p2, c, -1);
    }
}

void fill(Point* fp, Color* c) {
    Color oldColor;
    // cek warna apakah sudah sama
    getColor(fp, &oldColor);
    if (isColorSame(&oldColor, c)) {
        // do nothing
    } else {
        drawPoint(fp, c); // warnain titik itu

        Point next;
        int yBawah = fp -> y + 1;
        int yAtas = fp -> y - 1;
        int xKanan = fp -> x + 1;
        int xKiri = fp -> x - 1;
        if (yAtas >= 0) { // warnain atasnya
            setPoint(&next, fp -> x, yAtas);
            fill(&next, c);
        }
        if (xKanan < vinfo.xres) { // warnain kanannya
            setPoint(&next, xKanan, fp -> y);
            fill(&next, c);
        }
        if (xKiri >= 0) { // warnain kirinya
            setPoint(&next, xKiri, fp -> y);
            fill(&next, c);
        }
        if (yBawah < vinfo.yres) { // warnain bawahnya
            setPoint(&next, fp -> x, yBawah);
            fill(&next, c);
        }
    }
}

void drawPlaneDestroyed() {
    // !!!!! pesawat berantakan belum digambar !!!!!
    Color c;
    Point* fire; // kumpulan titik yang membentuk gambar api
    Point fp; // firepoint
    int i, j;
    
    fire = (Point*) malloc(6 * sizeof(Point));
    setPoint(&fire[0], headPlane, 90);
    setPoint(&fire[1], headPlane + 60, 20);
    setPoint(&fire[2], headPlane + 160, 30);
    setPoint(&fire[3], headPlane + 260, 100);
    setPoint(&fire[4], headPlane + 150, 130);
    setPoint(&fire[5], headPlane + 20, 100);
    setPoint(&fp, headPlane + 45, 55);
    // gambar pesawat meledak lama-lama menghilang
    for (i = 255; i >= 0; i -= 5) {
        setColor(&c, i, i, 0);
        for(j = 0; j < 6; j++) {
            drawLine(&fire[j], &fire[(j + 1) % 6], &c);
        }
        fill(&fp, &c);
        usleep(500);
    }
}

void* drawPlane() {
    Color c;
    Point* plane; // kumpulan titik yang membentuk gambar pesawat
    Point fp; //firepoint
    int i;
    int lengthPlane = 260; // panjang pesawat dari head sampai tail
    tailPlane = vinfo.xres;
    headPlane = tailPlane - lengthPlane;

    setColor(&c, 255, 0, 0);
    plane = (Point*) malloc(6 * sizeof(Point));
    while (1) {
        while (headPlane > 0) { // selama pesawat belum mentok di kiri
            if (kaboom) { // pesawat kena tembak
                drawPlaneDestroyed();
                kaboom = 0; // pesawat baru muncul lagi
                break;
            } else { // masih terbang
                setPoint(&plane[0], headPlane, 90);
                setPoint(&plane[1], headPlane + 40, 50);
                setPoint(&plane[2], headPlane + 190, 50);
                setPoint(&plane[3], headPlane + 220, 20);
                setPoint(&plane[4], headPlane + 260, 20);
                setPoint(&plane[5], headPlane + 190, 90);
                setPoint(&fp, headPlane + 45, 55);
                // gambar
                for(i = 0; i < 6; i++) {
                    drawLine(&plane[i], &plane[(i + 1) % 6], &c);
                }
                fill(&fp, &c);
                usleep(10000);
                // hapus
                fill(&fp, &bg);
                headPlane -= 20;
                tailPlane -= 20;
            }
        }
        tailPlane = vinfo.xres; // restart dari ujung kanan lagi
        headPlane = tailPlane - lengthPlane;
    }
}

void* drawGun() {
    Point* gun;
    Point fp;
    Color c;
    int half = vinfo.xres / 2;
    int moveLeft = 1; // moveLeft = 1 berarti arah mulut geser ke kiri
    int lengthGun = 100; // panjang dari bawah sampe atas
    int i;

    gun = (Point*) malloc(8 * sizeof(Point));
    setPoint(&gun[0], half - 65, vinfo.yres - 1);
    setPoint(&gun[1], half - 65, vinfo.yres - 10);
    setPoint(&gun[2], half - 10, vinfo.yres - 10);
    setPoint(&gun[3], half - 5, vinfo.yres -lengthGun); // moncong pistol
    setPoint(&gun[4], half + 5, vinfo.yres -lengthGun); // moncong pistol
    setPoint(&gun[5], half + 10, vinfo.yres - 10);
    setPoint(&gun[6], half + 65, vinfo.yres - 10);
    setPoint(&gun[7], half + 65, vinfo.yres - 1);
    setPoint(&fp, half, vinfo.yres - 15);
    setColor(&c, 255, 255, 255);
    
    srcYBeam = vinfo.yres - lengthGun;
    destXBeam = vinfo.xres;
    // konstanta-konstanta untuk menghitung srcXBeam, didapat dari penurunan rumus
    int constA = vinfo.xres * lengthGun / 2;
    int constB = vinfo.xres * vinfo.yres / 2;
    while (1) {
        // penembak ganti arah bolak balik kiri kanan
        if (moveLeft) {
            destXBeam -= 5;
            if (destXBeam <= 0) {
                moveLeft = 0;
            }
        } else {
            destXBeam += 5;
            if (destXBeam >= vinfo.xres) {
                moveLeft = 1;
            }
        }
        // moncong pistolnya bergeser berarti srcXBeam juga bergeser
        srcXBeam = (destXBeam * lengthGun + constB - constA) / vinfo.yres;
        setPoint(&gun[3], srcXBeam - 5, srcYBeam);
        setPoint(&gun[4], srcXBeam + 5, srcYBeam);
        //gambar
        for (i = 0; i < 8; i++) {
            drawLine(&gun[i], &gun[(i + 1) % 8], &c);
        }
        fill(&fp, &c);
        usleep(1000);
        // hapus bagian bergeraknya saja
        drawLine(&gun[1], &gun[6], &bg);
        fill(&fp, &bg);
    }
}

void* drawBullet() {
    Point srcBeam, destBeam;
    char stroke;
    while(1) {
        if (!kaboom) { // pesawat lagi animasi hancur maka gabisa nembak
            stroke = fgetc(stdin);
            if (stroke == 10) { // ENTER
                setPoint(&srcBeam, srcXBeam, srcYBeam - 10);
                setPoint(&destBeam, destXBeam, 0);
                // !!!!! peluru belum digambar, baru kena/nggak doang !!!!!
                if (destXBeam > headPlane && destXBeam < tailPlane) { // kena
                    kaboom = 1;
                }
            }
        }
    }
}

void connectBuffer() {
    // Open the file for reading and writing
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        perror("Error: cannot open framebuffer device");
        exit(1);
    }
    // Get fixed screen information
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        exit(2);
    }
    // Get variable screen information
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        exit(3);
    }
    // Figure out the size of the screen in bytes
    bytePerPixel = vinfo.bits_per_pixel / 8;
    screensize = (vinfo.xres + overPixel) * vinfo.yres * bytePerPixel;
    // Map the device to memory
    fbp = (char*) mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if ((int) fbp == -1) {
        perror("Error: failed to map framebuffer device to memory");
        exit(4);
    }
}

int main() {
    connectBuffer();
    setColor(&bg, 0, 0, 0);
    clearScreen(&bg);
    pthread_t thrPlane, thrGun, thrBullet;
    pthread_create(&thrPlane, NULL, drawPlane, "thrPlane");
    pthread_create(&thrGun, NULL, drawGun, "thrGun");
    pthread_create(&thrBullet, NULL, drawBullet, "thrBullet");
    pthread_join(thrBullet, NULL); // infinity loop
    munmap(fbp, screensize);
    close(fbfd);
    return 0;
}
