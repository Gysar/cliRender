#include "cmdrender.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ── Platform: Windows ─────────────────────────────────────────────────────────

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void ClearScreen(void)
{
    HANDLE hStdOut;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;
    COORD homeCoords = {0, 0};

    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleScreenBufferInfo(hStdOut, &csbi)) return;
    cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    if (!FillConsoleOutputCharacter(hStdOut, (TCHAR)' ', cellCount, homeCoords, &count)) return;
    if (!FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, cellCount, homeCoords, &count)) return;
    SetConsoleCursorPosition(hStdOut, homeCoords);
}

void hideCursor(void)
{
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info = {1, FALSE};
    SetConsoleCursorInfo(hStdOut, &info);
}

void showCursor(void)
{
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info = {1, TRUE};
    SetConsoleCursorInfo(hStdOut, &info);
}

void disableQuickEdit(void)
{
    HANDLE hConIn = CreateFile("CONIN$",
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hConIn == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    GetConsoleMode(hConIn, &mode);
    mode = (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(hConIn, mode);
    CloseHandle(hConIn);
}

// width  = number of columns (horizontal)
// height = number of rows    (vertical)
void drawMatrix(char **matrix, int width, int height, bool xoffset)
{
    static CHAR_INFO *ci = NULL;
    static int ci_cap = 0;
    // xoffset doubles each character with a trailing space for square pixels
    int cols   = xoffset ? width * 2 : width;
    int needed = height * cols;
    if (needed > ci_cap)
    {
        free(ci);
        ci = (CHAR_INFO *)malloc(needed * sizeof(CHAR_INFO));
        ci_cap = needed;
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hideCursor();

    int row, col;
    for (row = 0; row < height; row++)
    {
        for (col = 0; col < width; col++)
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

    COORD bufSize    = {(SHORT)cols,     (SHORT)height};
    COORD bufOrigin  = {0, 0};
    SMALL_RECT region = {0, 0, (SHORT)(cols - 1), (SHORT)(height - 1)};
    WriteConsoleOutput(hOut, ci, bufSize, bufOrigin, &region);
}

// ── Platform: POSIX ───────────────────────────────────────────────────────────

#else
#include <unistd.h>
#include <term.h>

void ClearScreen(void)
{
    if (!cur_term)
    {
        int result;
        setupterm(NULL, STDOUT_FILENO, &result);
        if (result <= 0) return;
    }
    putp(tigetstr("clear"));
}

void hideCursor(void)
{
    printf("\033[?25l");
    fflush(stdout);
}

void showCursor(void)
{
    printf("\033[?25h");
    fflush(stdout);
}

void disableQuickEdit(void) {}

// width  = number of columns (horizontal)
// height = number of rows    (vertical)
void drawMatrix(char **matrix, int width, int height, bool xoffset)
{
    static char *framebuf = NULL;
    static int framebuf_cap = 0;
    // each row: width chars (+ space per char if xoffset) + newline
    int needed = height * (xoffset ? width * 2 + 1 : width + 1);
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
        for (i = 0; i < height; i++)
        {
            memcpy(framebuf + pos, matrix[i], width);
            pos += width;
            framebuf[pos++] = '\n';
        }
    }
    else
    {
        int i;
        for (i = 0; i < height; i++)
        {
            int j;
            for (j = 0; j < width; j++)
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

// ── Matrix ────────────────────────────────────────────────────────────────────

// width  = number of columns (horizontal)
// height = number of rows    (vertical)
void initializeMatrix(char **matrix, int width, int height)
{
    int i;
    for (i = 0; i < height; i++)
    {
        int j;
        for (j = 0; j < width; j++)
            matrix[i][j] = ' ';
        matrix[i][width] = 0;   // null-terminate each row
    }
}

// ── Projection & Points ───────────────────────────────────────────────────────

Point makePoint(char symbol, int x, int y)
{
    Point p = {symbol, x, y, true};
    return p;
}

Point makeDefaultPoint(int x, int y)
{
    return makePoint('O', x, y);
}

// Projects a 3D world point onto the 2D screen matrix.
// World coordinate system:
//   X: horizontal (left = -1, right = +1 at z=1)
//   Y: vertical   (down = -1, up   = +1 at z=1)
//   Z: depth      (camera at z=0, scene in front at z>0)
// Result: Point.x = row index (vertical), Point.y = col index (horizontal)
Point projectPoint(DDDPoint p, int screenWidth, int screenHeight)
{
    Point result;
    result.symbol  = 'O';
    result.visible = false;

    if (p.z <= 0.0001) return result;

    double xt = p.x / p.z;   // normalized horizontal position (-1 .. +1)
    double yt = p.y / p.z;   // normalized vertical   position (-1 .. +1)

    int col = (int)(0.5 * screenWidth  * (xt + 1));   // world X -> column
    int row = (int)(0.5 * screenHeight * (yt + 1));   // world Y -> row

    if (row < 0 || row >= screenHeight || col < 0 || col >= screenWidth)
        return result;

    result.x = row;   // Point.x = row (vertical)
    result.y = col;   // Point.y = col (horizontal)
    result.visible = true;
    return result;
}

// ── Rotation ──────────────────────────────────────────────────────────────────

DDDPoint rotateZ(DDDPoint p, double a)
{
    a = a * 3.1415926 / 180.0;
    double ca = cos(a), sa = sin(a);
    DDDPoint pp = {p.x * ca - p.y * sa, p.x * sa + p.y * ca, p.z};
    return pp;
}

DDDPoint rotateY(DDDPoint p, double a)
{
    a = a * 3.1415926 / 180.0;
    double ca = cos(a), sa = sin(a);
    DDDPoint pp = {p.x * ca + p.z * sa, p.y, -p.x * sa + p.z * ca};
    return pp;
}

DDDPoint rotateX(DDDPoint p, double a)
{
    a = a * 3.1415926 / 180.0;
    double ca = cos(a), sa = sin(a);
    DDDPoint pp = {p.x, p.y * ca - p.z * sa, p.y * sa + p.z * ca};
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

// Rotates all vertices and line endpoints around the object's centroid.
void rotateObject(Object o, DDDPoint (*f)(DDDPoint, double), double a)
{
    DDDPoint c = getCenter(o);
    int i;
    for (i = 0; i < o.verticesCount; i++)
    {
        DDDPoint v = o.vertices[i];
        v.x -= c.x; v.y -= c.y; v.z -= c.z;
        v = (*f)(v, a);
        v.x += c.x; v.y += c.y; v.z += c.z;
        o.vertices[i] = v;
    }
    for (i = 0; i < o.linesCount; i++)
    {
        DDDPoint s = o.lines[i].start;
        DDDPoint e = o.lines[i].end;
        s.x -= c.x; s.y -= c.y; s.z -= c.z;
        e.x -= c.x; e.y -= c.y; e.z -= c.z;
        s = (*f)(s, a);
        e = (*f)(e, a);
        s.x += c.x; s.y += c.y; s.z += c.z;
        e.x += c.x; e.y += c.y; e.z += c.z;
        o.lines[i].start = s;
        o.lines[i].end   = e;
    }
}

// ── Translation ───────────────────────────────────────────────────────────────

void translateObjectByX(Object *o, double dx)
{
    int i;
    for (i = 0; i < o->verticesCount; i++) o->vertices[i].x += dx;
    for (i = 0; i < o->linesCount; i++)
    {
        o->lines[i].start.x += dx;
        o->lines[i].end.x   += dx;
    }
}
void translateObjectToX(Object *o, double x)
{
    DDDPoint c = getCenter(*o);
    translateObjectByX(o, x - c.x);
}
// dx in grid columns; width = total number of columns
void translateObjectByXGrid(Object *o, int dx, int width)
{
    translateObjectByX(o, 2.0 * dx / width);
}
void translateObjectToXGrid(Object *o, int x, int width)
{
    DDDPoint c = getCenter(*o);
    translateObjectByX(o, (2.0 * x / width - 1.0) - c.x);
}

void translateObjectByY(Object *o, double dy)
{
    int i;
    for (i = 0; i < o->verticesCount; i++) o->vertices[i].y += dy;
    for (i = 0; i < o->linesCount; i++)
    {
        o->lines[i].start.y += dy;
        o->lines[i].end.y   += dy;
    }
}
void translateObjectToY(Object *o, double y)
{
    DDDPoint c = getCenter(*o);
    translateObjectByY(o, y - c.y);
}
// dy in grid rows; height = total number of rows
void translateObjectByYGrid(Object *o, int dy, int height)
{
    translateObjectByY(o, 2.0 * dy / height);
}
void translateObjectToYGrid(Object *o, int y, int height)
{
    DDDPoint c = getCenter(*o);
    translateObjectByY(o, (2.0 * y / height - 1.0) - c.y);
}

void translateObjectByZ(Object *o, double dz)
{
    int i;
    for (i = 0; i < o->verticesCount; i++) o->vertices[i].z += dz;
    for (i = 0; i < o->linesCount; i++)
    {
        o->lines[i].start.z += dz;
        o->lines[i].end.z   += dz;
    }
}
void translateObjectToZ(Object *o, double z)
{
    DDDPoint c = getCenter(*o);
    translateObjectByZ(o, z - c.z);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

// Returns the ASCII character that best represents a line segment's direction.
// x0/y0 are row/col indices: dx = row difference, dy = col difference.
char lineSymbol(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0;   // row delta  (vertical   change)
    int dy = y1 - y0;   // col delta  (horizontal change)
    if (dx == 0 && dy == 0) return '.';
    double slope = (dx == 0) ? 1e9 : fabs((double)dy / (double)dx);
    if (slope < 0.414) return '|';    // nearly vertical
    if (slope > 2.414) return '_';    // nearly horizontal
    if ((dx > 0 && dy < 0) || (dx < 0 && dy > 0)) return '/';
    return '\\';
}

// Bresenham line rasterizer.
// x0/y0 are row/col indices; width = col bound, height = row bound.
void rasterizeLine(char **matrix, int x0, int y0, int x1, int y1,
                   char sym, int width, int height, bool skipEndpoints)
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
            // x0 = row (< height), y0 = col (< width)
            if (x0 >= 0 && x0 < height && y0 >= 0 && y0 < width)
                matrix[x0][y0] = sym;
        }
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void addPoint(char **matrix, Point p, int width, int height)
{
    if (!p.visible) return;
    // p.x = row (< height), p.y = col (< width)
    if (p.x < 0 || p.x >= height || p.y < 0 || p.y >= width) return;
    matrix[p.x][p.y] = p.symbol;
}

void addLine(char **matrix, Line l, int width, int height)
{
    Point p0 = projectPoint(l.start, width - 1, height - 1);
    Point p1 = projectPoint(l.end,   width - 1, height - 1);
    if (!p0.visible || !p1.visible) return;
    char sym = lineSymbol(p0.x, p0.y, p1.x, p1.y);
    rasterizeLine(matrix, p0.x, p0.y, p1.x, p1.y, sym, width - 1, height - 1, true);
}

void removeLine(char **matrix, Line l, int width, int height)
{
    Point p0 = projectPoint(l.start, width - 1, height - 1);
    Point p1 = projectPoint(l.end,   width - 1, height - 1);
    if (!p0.visible || !p1.visible) return;
    rasterizeLine(matrix, p0.x, p0.y, p1.x, p1.y, ' ', width - 1, height - 1, true);
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
        addLine(matrix, o.lines[i], width, height);
}

void removeObject(char **matrix, Object o, int width, int height)
{
    int i;
    for (i = 0; i < o.verticesCount; i++)
    {
        Point p = projectPoint(o.vertices[i], width - 1, height - 1);
        if (!p.visible) continue;
        // p.x = row (< height-1), p.y = col (< width-1)
        if (p.x < 0 || p.x >= height - 1 || p.y < 0 || p.y >= width - 1) continue;
        matrix[p.x][p.y] = ' ';
    }
    for (i = 0; i < o.linesCount; i++)
        removeLine(matrix, o.lines[i], width, height);
}

// ── Sleep ─────────────────────────────────────────────────────────────────────

#ifdef _WIN32
// Sleep() is already available via windows.h above
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h>
#else
#include <unistd.h>
#endif

void sleep_ms(int milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec  = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    if (milliseconds >= 1000) sleep(milliseconds / 1000);
    usleep((milliseconds % 1000) * 1000);
#endif
}

// ── Animation ─────────────────────────────────────────────────────────────────

// Wrappers: bind rotateObject to the AnimFunc signature (Object*, double)
void animRotateX(Object *o, double a) { rotateObject(*o, rotateX, a); }
void animRotateY(Object *o, double a) { rotateObject(*o, rotateY, a); }
void animRotateZ(Object *o, double a) { rotateObject(*o, rotateZ, a); }

// Pre-calculate an AnimationStep from an AnimationDef and the global fps
AnimationStep calcStep(AnimationDef def, double fps)
{
    AnimationStep step;
    step.func = def.func;
    int totalSteps = (int)(fps * def.durationMS / 1000.0);
    if (totalSteps <= 0) totalSteps = 1;
    step.amountPerStep  = def.totalAmount / totalSteps;
    step.remainingSteps = totalSteps;
    return step;
}

// Run the scene: each frame, remove all objects, apply all active animation
// steps, redraw all objects. Runs until every step of every object is done.
void runScene(Scene *scene, double fps,
              char **matrix, int width, int height, bool xoffset)
{
    int sleepdurationMS = (int)(1000.0 / fps);
    int i, j;
    int anyActive = 1;
    while (anyActive)
    {
        // Early-exit scan: stop as soon as one active step is found
        anyActive = 0;
        for (i = 0; !anyActive && i < scene->count; i++)
            for (j = 0; !anyActive && j < scene->objects[i].stepsCount; j++)
                if (scene->objects[i].steps[j].remainingSteps > 0)
                    anyActive = 1;
        if (!anyActive) break;

        // Remove all objects from the matrix
        for (i = 0; i < scene->count; i++)
            removeObject(matrix, *scene->objects[i].o, width, height);

        // Apply one step per active animation per object
        for (i = 0; i < scene->count; i++)
        {
            for (j = 0; j < scene->objects[i].stepsCount; j++)
            {
                AnimationStep *step = &scene->objects[i].steps[j];
                if (step->remainingSteps > 0)
                {
                    step->func(scene->objects[i].o, step->amountPerStep);
                    step->remainingSteps--;
                }
            }
        }

        // Redraw all objects
        for (i = 0; i < scene->count; i++)
            addObject(matrix, *scene->objects[i].o, width, height);

        sleep_ms(sleepdurationMS);
        drawMatrix(matrix, width, height, xoffset);
    }
}

// ── Objects ───────────────────────────────────────────────────────────────────

Object getCube(void)
{
    DDDPoint pp1 = {-0.25,  0.25, 1.5};
    DDDPoint pp2 = {-0.25, -0.25, 1.5};
    DDDPoint pp3 = { 0.25,  0.25, 1.5};
    DDDPoint pp4 = { 0.25, -0.25, 1.5};
    DDDPoint pp5 = {-0.25,  0.25, 2.0};
    DDDPoint pp6 = {-0.25, -0.25, 2.0};
    DDDPoint pp7 = { 0.25,  0.25, 2.0};
    DDDPoint pp8 = { 0.25, -0.25, 2.0};

    DDDPoint tmpVerts[] = {pp1, pp2, pp3, pp4, pp5, pp6, pp7, pp8};
    Line     tmpEdges[] = {
        {pp1,pp2},{pp1,pp3},{pp2,pp4},{pp3,pp4},
        {pp5,pp6},{pp5,pp7},{pp6,pp8},{pp7,pp8},
        {pp1,pp5},{pp2,pp6},{pp3,pp7},{pp4,pp8}
    };

    Object o;
    o.verticesCount = 8;
    o.vertices = (DDDPoint *)malloc(o.verticesCount * sizeof(DDDPoint));
    memcpy(o.vertices, tmpVerts, o.verticesCount * sizeof(DDDPoint));
    o.linesCount = 12;
    o.lines = (Line *)malloc(o.linesCount * sizeof(Line));
    memcpy(o.lines, tmpEdges, o.linesCount * sizeof(Line));
    return o;
}

// Loads a Wavefront OBJ file.
// Vertices are read from 'v' lines, edges are derived from 'f' (face) lines.
// Duplicate edges are removed. Supports 'v/vt/vn' face format.
Object getFromOBJ(const char *filename)
{
    Object o;
    o.vertices = NULL; o.verticesCount = 0;
    o.lines    = NULL; o.linesCount    = 0;

    FILE *fp = fopen(filename, "r");
    if (!fp) return o;

    int vCount = 0, fCount = 0, maxFaceVerts = 0;
    char buf[512];

    // Pass 1: count vertices and faces, find maximum face valence
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
                while (*p == ' ') p++;
                if (*p && *p != '\n') { n++; while (*p && *p != ' ' && *p != '\n') p++; }
            }
            if (n > maxFaceVerts) maxFaceVerts = n;
        }
    }
    if (vCount == 0 || fCount == 0 || maxFaceVerts < 2) { fclose(fp); return o; }

    o.vertices   = (DDDPoint *)malloc(vCount * sizeof(DDDPoint));
    int maxEdges = fCount * maxFaceVerts;
    int *edgeA   = (int *)malloc(maxEdges * sizeof(int));
    int *edgeB   = (int *)malloc(maxEdges * sizeof(int));
    int edgeCount = 0;
    int *faceVerts = (int *)malloc(maxFaceVerts * sizeof(int));

    // Pass 2: parse vertices and build deduplicated edge list
    rewind(fp);
    while (fgets(buf, sizeof(buf), fp))
    {
        if (buf[0] == 'v' && buf[1] == ' ')
        {
            double v1, v2, v3;
            sscanf(buf + 2, "%lf %lf %lf", &v1, &v2, &v3);
            // v1 = horizontal (X in OBJ), v2 = vertical (Y in OBJ, negated so up=top),
            // v3 = depth
            DDDPoint pt = {v1, -v2, v3};
            o.vertices[o.verticesCount++] = pt;
        }
        else if (buf[0] == 'f' && buf[1] == ' ')
        {
            int faceVertCount = 0;
            char *p = buf + 2;
            while (*p && *p != '\n')
            {
                while (*p == ' ') p++;
                if (!*p || *p == '\n') break;
                int vi;
                sscanf(p, "%d", &vi);               // reads index before optional '/'
                faceVerts[faceVertCount++] = vi - 1; // OBJ indices are 1-based
                while (*p && *p != ' ' && *p != '\n') p++;
            }
            int i;
            for (i = 0; i < faceVertCount; i++)
            {
                int a = faceVerts[i];
                int b = faceVerts[(i + 1) % faceVertCount];
                // Normalize edge so (min, max) for deduplication
                int ea = a < b ? a : b;
                int eb = a < b ? b : a;
                bool dup = false;
                int j;
                for (j = 0; j < edgeCount; j++)
                    if (edgeA[j] == ea && edgeB[j] == eb) { dup = true; break; }
                if (!dup) { edgeA[edgeCount] = ea; edgeB[edgeCount] = eb; edgeCount++; }
            }
        }
    }
    fclose(fp);

    o.lines      = (Line *)malloc(edgeCount * sizeof(Line));
    o.linesCount = edgeCount;
    int i;
    for (i = 0; i < edgeCount; i++)
    {
        o.lines[i].start = o.vertices[edgeA[i]];
        o.lines[i].end   = o.vertices[edgeB[i]];
    }
    free(faceVerts); free(edgeA); free(edgeB);
    return o;
}

void freeObject(Object *o)
{
    free(o->vertices); o->vertices = NULL; o->verticesCount = 0;
    free(o->lines);    o->lines    = NULL; o->linesCount    = 0;
}

// Returns the smallest Z value across all vertices.
// Used to compute the offset needed to place the object in front of the camera.
double smallestZValue(Object o)
{
    int i;
    double smallest = 1e9;
    for (i = 0; i < o.verticesCount; i++)
        if (o.vertices[i].z < smallest) smallest = o.vertices[i].z;
    return smallest;
}

// Shift all vertices and line endpoints of an object forward so that
// its closest point is at z = 1 (just in front of the camera).
void pushInFront(Object *o)
{
    double offset = 1.0 - smallestZValue(*o);
    int i;
    for (i = 0; i < o->verticesCount; i++)
        o->vertices[i].z += offset;
    for (i = 0; i < o->linesCount; i++)
    {
        o->lines[i].start.z += offset;
        o->lines[i].end.z   += offset;
    }
}