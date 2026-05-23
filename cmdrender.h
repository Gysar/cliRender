#ifndef CMDRENDER_H
#define CMDRENDER_H

#include <stdbool.h>

// ── Structs ───────────────────────────────────────────────────────────────────

struct point {
    char symbol;
    int x;       // row index  (vertical,   maps to world Y)
    int y;       // col index  (horizontal, maps to world X)
    bool visible;
};
typedef struct point Point;

struct dddpoint {
    double x;   // horizontal (left / right)
    double y;   // vertical   (up   / down )
    double z;   // depth      (camera distance)
};
typedef struct dddpoint DDDPoint;

struct line {
    DDDPoint start;
    DDDPoint end;
};
typedef struct line Line;

struct object {
    DDDPoint *vertices;
    int verticesCount;
    Line *lines;
    int linesCount;
};
typedef struct object Object;

// ── Platform ──────────────────────────────────────────────────────────────────

void ClearScreen(void);
void hideCursor(void);
void showCursor(void);
void disableQuickEdit(void);

// ── Matrix ────────────────────────────────────────────────────────────────────
// width  = number of columns (horizontal extent)
// height = number of rows    (vertical   extent)

void initializeMatrix(char **matrix, int width, int height);
void drawMatrix(char **matrix, int width, int height, bool xoffset);

// ── Projection & Points ───────────────────────────────────────────────────────

Point makePoint(char symbol, int x, int y);
Point makeDefaultPoint(int x, int y);
Point projectPoint(DDDPoint p, int screenWidth, int screenHeight);

// ── Rotation ──────────────────────────────────────────────────────────────────

DDDPoint rotateX(DDDPoint p, double a);
DDDPoint rotateY(DDDPoint p, double a);
DDDPoint rotateZ(DDDPoint p, double a);
DDDPoint getCenter(Object o);
void rotateObject(Object o, DDDPoint (*f)(DDDPoint, double), double a);

// ── Translation ───────────────────────────────────────────────────────────────

void translateObjectByX(Object *o, double dx);
void translateObjectToX(Object *o, double x);
void translateObjectByXGrid(Object *o, int dx, int width);   // width  = screen width  (cols)
void translateObjectToXGrid(Object *o, int x,  int width);

void translateObjectByY(Object *o, double dy);
void translateObjectToY(Object *o, double y);
void translateObjectByYGrid(Object *o, int dy, int height);  // height = screen height (rows)
void translateObjectToYGrid(Object *o, int y,  int height);

void translateObjectByZ(Object *o, double dz);
void translateObjectToZ(Object *o, double z);

// ── Drawing ───────────────────────────────────────────────────────────────────

char lineSymbol(int x0, int y0, int x1, int y1);
void rasterizeLine(char **matrix, int x0, int y0, int x1, int y1,
                   char sym, int width, int height, bool skipEndpoints);
void addPoint(char **matrix, Point p, int width, int height);
void addLine(char **matrix, Line l, int width, int height);
void removeLine(char **matrix, Line l, int width, int height);
void addObject(char **matrix, Object o, int width, int height);
void removeObject(char **matrix, Object o, int width, int height);

// ── Animation ─────────────────────────────────────────────────────────────────

void sleep_ms(int milliseconds);

// Unified function type for all object manipulations (rotation & translation).
// translateObjectBy* already matches this signature directly.
// For rotations use the animRotateX/Y/Z wrappers below.
typedef void (*AnimFunc)(Object *o, double amount);

// What the caller specifies: function + total amount + duration
struct animationDef {
    AnimFunc func;
    double   totalAmount; // total angle (degrees) or total distance (world coords)
    int      durationMS;
};
typedef struct animationDef AnimationDef;

// Pre-calculated step used by the render loop
struct animationStep {
    AnimFunc func;
    double   amountPerStep;  // totalAmount / totalSteps
    int      remainingSteps;
};
typedef struct animationStep AnimationStep;

// An object together with its animation steps
struct animatedObject {
    Object        *o;
    AnimationStep *steps;
    int            stepsCount;
};
typedef struct animatedObject AnimatedObject;

// A collection of animated objects rendered together in one loop
struct scene
{
    AnimatedObject *objects;
    int count;
};
typedef struct scene Scene;

// Wrappers: bind rotateObject to the AnimFunc signature (Object*, double)
void animRotateX(Object *o, double a);
void animRotateY(Object *o, double a);
void animRotateZ(Object *o, double a);

// Pre-calculate an AnimationStep from an AnimationDef and the global fps
AnimationStep calcStep(AnimationDef def, double fps);

// Run the scene: apply all transformations and redraw every frame
// until all animation steps of all objects are exhausted.
void runScene(Scene *scene, double fps,
              char **matrix, int width, int height, bool xoffset);

// ── Objects ───────────────────────────────────────────────────────────────────

Object getCube(void);
Object getFromOBJ(const char *filename);
void freeObject(Object *o);
double smallestZValue(Object o);
void pushInFront(Object *o);

#endif // CMDRENDER_H