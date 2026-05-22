#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

struct point
{
    char symbol;
    int x;
    int y;
    bool visible;
};
typedef struct point Point;

struct dddpoint
{
    double x;
    double y;
    double z;
};
typedef struct dddpoint DDDPoint;

struct line
{
    DDDPoint start;
    DDDPoint end;
};
typedef struct line Line;

struct object
{
    DDDPoint *vertices;
    int verticesCount;
    Line *lines;
    int linesCount;
};
typedef struct object Object;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void ClearScreen()
{
    HANDLE hStdOut;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;
    COORD homeCoords = {0, 0};

    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut == INVALID_HANDLE_VALUE)
        return;

    if (!GetConsoleScreenBufferInfo(hStdOut, &csbi))
        return;
    cellCount = csbi.dwSize.X * csbi.dwSize.Y;

    if (!FillConsoleOutputCharacter(
            hStdOut,
            (TCHAR)' ',
            cellCount,
            homeCoords,
            &count))
        return;

    if (!FillConsoleOutputAttribute(
            hStdOut,
            csbi.wAttributes,
            cellCount,
            homeCoords,
            &count))
        return;

    SetConsoleCursorPosition(hStdOut, homeCoords);
}

#else // !_WIN32
#include <unistd.h>
#include <term.h>

void ClearScreen()
{
    if (!cur_term)
    {
        int result;
        setupterm(NULL, STDOUT_FILENO, &result);
        if (result <= 0)
            return;
    }

    putp(tigetstr("clear"));
}
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void hideCursor()
{
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info = {1, FALSE};
    SetConsoleCursorInfo(hStdOut, &info);
}

void showCursor()
{
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info = {1, TRUE};
    SetConsoleCursorInfo(hStdOut, &info);
}

void disableQuickEdit()
{
    HANDLE hConIn = CreateFile("CONIN$",
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hConIn == INVALID_HANDLE_VALUE)
        return;
    DWORD mode = 0;
    GetConsoleMode(hConIn, &mode);
    mode = (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(hConIn, mode);
    CloseHandle(hConIn);
}

void drawMatrix(char **matrix, int width, int height, bool xoffset)
{
    static CHAR_INFO *ci = NULL;
    static int ci_cap = 0;
    int cols = xoffset ? height * 2 : height;
    int needed = width * cols;
    if (needed > ci_cap)
    {
        free(ci);
        ci = (CHAR_INFO *)malloc(needed * sizeof(CHAR_INFO));
        ci_cap = needed;
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hideCursor();

    int row, col;
    for (row = 0; row < width; row++)
    {
        for (col = 0; col < height; col++)
        {
            int idx = row * cols + (xoffset ? col * 2 : col);
            ci[idx].Char.AsciiChar = matrix[row][col];
            ci[idx].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            if (xoffset)
            {
                ci[idx + 1].Char.AsciiChar = ' ';
                ci[idx + 1].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            }
        }
    }

    COORD bufSize = {(SHORT)cols, (SHORT)width};
    COORD bufOrigin = {0, 0};
    SMALL_RECT region = {0, 0, (SHORT)(cols - 1), (SHORT)(width - 1)};
    WriteConsoleOutput(hOut, ci, bufSize, bufOrigin, &region);
}

#else // ── POSIX ────────────────────────────────────────────────────────────
#include <unistd.h>

void hideCursor()
{
    printf("\033[?25l");
    fflush(stdout);
}
void showCursor()
{
    printf("\033[?25h");
    fflush(stdout);
}
void disableQuickEdit() {}

void drawMatrix(char **matrix, int width, int height, bool xoffset)
{
    static char *framebuf = NULL;
    static int framebuf_cap = 0;
    int needed = width * (xoffset ? height * 2 + 1 : height + 1);
    if (needed > framebuf_cap)
    {
        free(framebuf);
        framebuf = (char *)malloc(needed);
        framebuf_cap = needed;
    }
    int pos = 0;
    printf("\033[H");
    if (!xoffset)
    {
        int i;
        for (i = 0; i < width; i++)
        {
            memcpy(framebuf + pos, matrix[i], height);
            pos += height;
            framebuf[pos++] = '\n';
        }
    }
    else
    {
        int i;
        for (i = 0; i < width; i++)
        {
            int j;
            for (j = 0; j < height; j++)
            {
                framebuf[pos++] = matrix[i][j];
                framebuf[pos++] = ' ';
            }
            framebuf[pos++] = '\n';
        }
    }
    hideCursor();
    fwrite(framebuf, 1, pos, stdout);
    fflush(stdout);
}
#endif

void initializeMatrix(char **matrix, int width, int height)
{
    int i;
    for (i = 0; i < width; i++)
    {
        int j;
        for (j = 0; j < height; j++)
        {
            matrix[i][j] = ' ';
        }
        matrix[i][height] = 0;
    }
}

Point makePoint(char symbol, int x, int y)
{
    Point p = {symbol, x, y, true};
    return p;
}

Point makeDefaultPoint(int x, int y)
{
    return makePoint('O', x, y);
}

Point projectPoint(DDDPoint p, int screenWidth, int screenHeight)
{

    /*
    screen: z=1
    0/0/1 = screen middle
    left-right: -1 - 1
    down-up: -1 - 1
    */

    Point result;
    result.symbol = 'O';
    result.visible = false;

    if (p.z <= 0.0001)
        return result;

    double xt = (p.x / p.z);
    double yt = (p.y / p.z);
    int x = (int)(0.5 * screenWidth * (xt + 1));
    int y = (int)(0.5 * screenHeight * (yt + 1));

    if (x < 0 || x >= screenWidth || y < 0 || y >= screenHeight)
        return result;

    result.x = x;
    result.y = y;
    result.visible = true;
    return result;
}

DDDPoint rotateZ(DDDPoint p, double a)
{
    a = a * 3.1415926 / 180.0;
    double ca = cos(a);
    double sa = sin(a);
    double x = p.x * ca - p.y * sa;
    double y = p.x * sa + p.y * ca;

    DDDPoint pp = {x, y, p.z};
    return pp;
}

DDDPoint rotateY(DDDPoint p, double a)
{
    a = a * 3.1415926 / 180.0;
    double ca = cos(a);
    double sa = sin(a);
    double x = p.x * ca + p.z * sa;
    double z = -p.x * sa + p.z * ca;

    DDDPoint pp = {x, p.y, z};
    return pp;
}

DDDPoint rotateX(DDDPoint p, double a)
{
    a = a * 3.1415926 / 180.0;
    double ca = cos(a);
    double sa = sin(a);
    double y = p.y * ca - p.z * sa;
    double z = p.y * sa + p.z * ca;

    DDDPoint pp = {p.x, y, z};
    return pp;
}

DDDPoint getCenter(Object o)
{
    DDDPoint center = {0, 0, 0};
    int i;
    for (i = 0; i < o.verticesCount; i++)
    {
        center.x += o.vertices[i].x;
        center.y += o.vertices[i].y;
        center.z += o.vertices[i].z;
    }
    center.x /= o.verticesCount;
    center.y /= o.verticesCount;
    center.z /= o.verticesCount;
    return center;
}

void rotateObject(Object o, DDDPoint (*f)(DDDPoint, double), double a)
{
    DDDPoint c = getCenter(o);
    int i;
    for (i = 0; i < o.verticesCount; i++)
    {
        DDDPoint v = o.vertices[i];
        v.x -= c.x;
        v.y -= c.y;
        v.z -= c.z;
        v = (*f)(v, a);
        v.x += c.x;
        v.y += c.y;
        v.z += c.z;
        o.vertices[i] = v;
    }
    for (i = 0; i < o.linesCount; i++)
    {
        DDDPoint s = o.lines[i].start;
        DDDPoint e = o.lines[i].end;
        s.x -= c.x;
        s.y -= c.y;
        s.z -= c.z;
        e.x -= c.x;
        e.y -= c.y;
        e.z -= c.z;
        s = (*f)(s, a);
        e = (*f)(e, a);
        s.x += c.x;
        s.y += c.y;
        s.z += c.z;
        e.x += c.x;
        e.y += c.y;
        e.z += c.z;
        o.lines[i].start = s;
        o.lines[i].end = e;
    }
}

char lineSymbol(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx == 0 && dy == 0)
        return '.';
    double slope = (dx == 0) ? 1e9 : fabs((double)dy / (double)dx);
    if (slope < 0.414)
        return '|';
    if (slope > 2.414)
        return '_';
    if ((dx > 0 && dy < 0) || (dx < 0 && dy > 0))
        return '/';
    return '\\';
}

// Bresenham
void rasterizeLine(char **matrix, int x0, int y0, int x1, int y1, char sym, int width, int height, bool skipEndpoints)
{
    int startX = x0, startY = y0;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (!(x0 == x1 && y0 == y1))
    {
        bool isEndpoint = (x0 == startX && y0 == startY) || (x0 == x1 && y0 == y1);
        if (!skipEndpoints || !isEndpoint)
        {
            if (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height)
                matrix[x0][y0] = sym;
        }
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void addLine(char **matrix, Line l, int width, int height)
{
    Point p0 = projectPoint(l.start, width - 1, height - 1);
    Point p1 = projectPoint(l.end, width - 1, height - 1);
    if (!p0.visible || !p1.visible)
        return;
    char sym = lineSymbol(p0.x, p0.y, p1.x, p1.y);
    rasterizeLine(matrix, p0.x, p0.y, p1.x, p1.y, sym, width - 1, height - 1, true);
}

void removeLine(char **matrix, Line l, int width, int height)
{
    Point p0 = projectPoint(l.start, width - 1, height - 1);
    Point p1 = projectPoint(l.end, width - 1, height - 1);
    if (!p0.visible || !p1.visible)
        return;
    rasterizeLine(matrix, p0.x, p0.y, p1.x, p1.y, ' ', width - 1, height - 1, true);
}

void addPoint(char **matrix, Point p, int width, int height)
{
    if (!p.visible)
        return;
    if (p.x < 0 || p.x >= width || p.y < 0 || p.y >= height)
        return;
    matrix[p.x][p.y] = p.symbol;
}

void addObject(char **matrix, Object o, int width, int height)
{
    int i;
    for (i = 0; i < o.verticesCount; i++)
    {
        Point p = projectPoint(o.vertices[i], width - 1, height - 1);
        addPoint(matrix, p, width - 1, height - 1);
    }
    for (i = 0; i < o.linesCount; i++)
    {
        addLine(matrix, o.lines[i], width, height);
    }
}

void removeObject(char **matrix, Object o, int width, int height)
{
    int i;
    for (i = 0; i < o.verticesCount; i++)
    {
        Point p = projectPoint(o.vertices[i], width - 1, height - 1);
        if (!p.visible)
            continue;
        if (p.x < 0 || p.x >= width - 1 || p.y < 0 || p.y >= height - 1)
            continue;
        matrix[p.x][p.y] = ' ';
    }
    for (i = 0; i < o.linesCount; i++)
    {
        removeLine(matrix, o.lines[i], width, height);
    }
}

#ifdef WIN32
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h> // for nanosleep
#else
#include <unistd.h> // for usleep
#endif

void sleep_ms(int milliseconds)
{ // cross-platform sleep function
#ifdef _WIN32
    Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    if (milliseconds >= 1000)
        sleep(milliseconds / 1000);
    usleep((milliseconds % 1000) * 1000);
#endif
}

void animateRotation(double ax, double ay, double az, double fps, int durationMS, Object o, char **matrix, int width, int height, bool xoffset)
{
    long sleepdurationMS = (int)(1000 / fps);
    int steps = (int)(fps * ((double)durationMS / 1000));
    if (steps == 0)
        return;
    int i;
    double axStep = ax / steps;
    double ayStep = ay / steps;
    double azStep = az / steps;
    for (i = 0; i < durationMS; i += sleepdurationMS)
    {
        removeObject(matrix, o, width, height);
        if (ax != 0)
            rotateObject(o, rotateX, axStep);
        if (ay != 0)
            rotateObject(o, rotateY, ayStep);
        if (az != 0)
            rotateObject(o, rotateZ, azStep);
        addObject(matrix, o, width, height);
        sleep_ms(sleepdurationMS);
        drawMatrix(matrix, width, height, xoffset);
    }
}

Object getCube()
{
    DDDPoint pp1 = {-0.25,  0.25, 1.5};
    DDDPoint pp2 = {-0.25, -0.25, 1.5};
    DDDPoint pp3 = { 0.25,  0.25, 1.5};
    DDDPoint pp4 = { 0.25, -0.25, 1.5};
    DDDPoint pp5 = {-0.25,  0.25, 2.0};
    DDDPoint pp6 = {-0.25, -0.25, 2.0};
    DDDPoint pp7 = { 0.25,  0.25, 2.0};
    DDDPoint pp8 = { 0.25, -0.25, 2.0};

    DDDPoint tmpVerts[] = {pp1,pp2,pp3,pp4,pp5,pp6,pp7,pp8};
    Line     tmpEdges[] = {
        {pp1,pp2},{pp1,pp3},{pp2,pp4},{pp3,pp4},
        {pp5,pp6},{pp5,pp7},{pp6,pp8},{pp7,pp8},
        {pp1,pp5},{pp2,pp6},{pp3,pp7},{pp4,pp8}
    };

    Object o;
    o.verticesCount = 8;
    o.vertices = (DDDPoint*)malloc(o.verticesCount * sizeof(DDDPoint));
    memcpy(o.vertices, tmpVerts, o.verticesCount * sizeof(DDDPoint));

    o.linesCount = 12;
    o.lines = (Line*)malloc(o.linesCount * sizeof(Line));
    memcpy(o.lines, tmpEdges, o.linesCount * sizeof(Line));

    return o;
}

Object getFromOBJ(const char *filename)
{
    Object o;
    o.vertices = NULL;
    o.verticesCount = 0;
    o.lines = NULL;
    o.linesCount = 0;

    FILE *fp = fopen(filename, "r");
    if (!fp)
        return o;

    int vCount = 0, fCount = 0, maxFaceVerts = 0;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp))
    {
        if (buf[0] == 'v' && buf[1] == ' ')
        {
            vCount++;
        }
        else if (buf[0] == 'f' && buf[1] == ' ')
        {
            fCount++;
            int n = 0;
            char *p = buf + 2;
            while (*p && *p != '\n')
            {
                while (*p == ' ')
                    p++;
                if (*p && *p != '\n')
                {
                    n++;
                    while (*p && *p != ' ' && *p != '\n')
                        p++;
                }
            }
            if (n > maxFaceVerts)
                maxFaceVerts = n;
        }
    }
    if (vCount == 0 || fCount == 0 || maxFaceVerts < 2)
    {
        fclose(fp);
        return o;
    }

    o.vertices = (DDDPoint *)malloc(vCount * sizeof(DDDPoint));

    int maxEdges = fCount * maxFaceVerts;
    int *edgeA = (int *)malloc(maxEdges * sizeof(int));
    int *edgeB = (int *)malloc(maxEdges * sizeof(int));
    int edgeCount = 0;

    int *faceVerts = (int *)malloc(maxFaceVerts * sizeof(int));

    rewind(fp);
    while (fgets(buf, sizeof(buf), fp))
    {
        if (buf[0] == 'v' && buf[1] == ' ')
        {
            double x, y, z;
            sscanf(buf + 2, "%lf %lf %lf", &y, &x, &z);
            DDDPoint pt = {-x, y, z};
            o.vertices[o.verticesCount++] = pt;
        }
        else if (buf[0] == 'f' && buf[1] == ' ')
        {
            int faceVertCount = 0;
            char *p = buf + 2;
            while (*p && *p != '\n')
            {
                while (*p == ' ')
                    p++;
                if (!*p || *p == '\n')
                    break;
                int vi;
                sscanf(p, "%d", &vi);
                faceVerts[faceVertCount++] = vi - 1;
                while (*p && *p != ' ' && *p != '\n')
                    p++;
            }

            int i;
            for (i = 0; i < faceVertCount; i++)
            {
                int a = faceVerts[i];
                int b = faceVerts[(i + 1) % faceVertCount];

                int ea = a < b ? a : b;
                int eb = a < b ? b : a;

                bool dup = false;
                int j;
                for (j = 0; j < edgeCount; j++)
                {
                    if (edgeA[j] == ea && edgeB[j] == eb)
                    {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                {
                    edgeA[edgeCount] = ea;
                    edgeB[edgeCount] = eb;
                    edgeCount++;
                }
            }
        }
    }
    fclose(fp);

    o.lines = (Line *)malloc(edgeCount * sizeof(Line));
    o.linesCount = edgeCount;
    int i;
    for (i = 0; i < edgeCount; i++)
    {
        o.lines[i].start = o.vertices[edgeA[i]];
        o.lines[i].end = o.vertices[edgeB[i]];
    }

    free(faceVerts);
    free(edgeA);
    free(edgeB);
    return o;
}

void freeObject(Object *o)
{
    free(o->vertices);
    o->vertices = NULL;
    o->verticesCount = 0;
    free(o->lines);
    o->lines = NULL;
    o->linesCount = 0;
}

double smallestZValue(Object o){
    int i;
    double smallest=1e9;
    for(i=0;i<o.verticesCount;i++){
        double next=o.vertices[i].z;
        if(next<smallest){
            smallest=next;
        }
    }
    return smallest;
}

int main(int argc, char *argv[])
{
    int width = 200;
    int height = 200;

    char **matrix;
    matrix = (char **)calloc(width, sizeof(char *));
    int i;
    for (i = 0; i < width; i++)
    {
        matrix[i] = (char *)calloc(height + 1, sizeof(char));
    }
    initializeMatrix(matrix, width, height);
    disableQuickEdit();
    hideCursor();

    Object o=getCube();
    for(i = 1; i < argc; i++){
        if(strcmp(argv[i], "-w") == 0 && i+1 < argc){
            width = atoi(argv[++i]);
        } else if(strcmp(argv[i], "-h") == 0 && i+1 < argc){
            height = atoi(argv[++i]);
        } else if(strcmp(argv[i], "-f") == 0 && i+1 < argc){
            freeObject(&o);
            o = getFromOBJ(argv[++i]);
        }
    }
    double smallestZ=smallestZValue(o);
    double offset=(1-smallestZ);
    for (i = 0; i < o.verticesCount; i++)
        o.vertices[i].z += offset;
    for (i = 0; i < o.linesCount; i++)
    {
        o.lines[i].start.z += offset;
        o.lines[i].end.z += offset;
    }
    
    addObject(matrix, o, width, height);
    drawMatrix(matrix, width, height, true);

    double fps = 30;
    while (true)
    {
        animateRotation(90, 0, 0, fps, 500, o, matrix, width, height, true);
    }

    for (i = 0; i < width; i++)
        free(matrix[i]);
    freeObject(&o);
    free(matrix);
    return 0;
}