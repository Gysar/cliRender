#include "cmdrender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

int main(int argc, char *argv[])
{
    // width  = number of columns (horizontal)
    // height = number of rows    (vertical)
    int width  = 200;
    int height = 200;
    int i;

    // ── Objects ───────────────────────────────────────────────────────────────
    // Object 0: main object — loaded from -f, defaults to a cube
    Object main_obj = getCube();

    // Parse optional named arguments: -w <cols>  -h <rows>  -f <objfile>
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
            width = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
        {
            freeObject(&main_obj);
            main_obj = getFromOBJ(argv[++i]);
        }
    }

    // Object 1 & 2: two cubes positioned left and right of the main object
    Object cube1 = getCube();
    Object cube2 = getCube();

    pushInFront(&main_obj);
    pushInFront(&cube1);
    pushInFront(&cube2);

    // Place cubes to the left and right
    translateObjectByX(&cube1, -0.9);
    translateObjectByX(&cube2,  0.9);
    translateObjectByZ(&cube1, 0.3);
    translateObjectByZ(&cube2, 0.3);

    // ── Matrix ────────────────────────────────────────────────────────────────
    char **matrix = (char **)calloc(height, sizeof(char *));
    for (i = 0; i < height; i++)
        matrix[i] = (char *)calloc(width + 1, sizeof(char));
    initializeMatrix(matrix, width, height);

    disableQuickEdit();
    hideCursor();

    // Draw initial frame
    addObject(matrix, main_obj, width, height);
    addObject(matrix, cube1,    width, height);
    addObject(matrix, cube2,    width, height);
    drawMatrix(matrix, width, height, true);

    // ── Animation ─────────────────────────────────────────────────────────────
    double fps = 30.0;

    // Each AnimatedObject keeps its own step array.
    // Array size must match stepsCount in the AnimatedObject below.
    AnimationStep mainSteps[2];
    AnimationStep cube1Steps[2];
    AnimationStep cube2Steps[2];

    AnimatedObject objects[] = {
        { &main_obj, mainSteps,  2 },
        { &cube1,    cube1Steps, 2 },
        { &cube2,    cube2Steps, 2 },
    };
    // All objects in ONE scene so they animate simultaneously in a single call
    Scene scene = { objects, 3 };

    // Back-translation scene: moves all objects back to their original Y position
    AnimationStep mainStepsBack[1];
    AnimationStep cube1StepsBack[1];
    AnimationStep cube2StepsBack[1];

    AnimatedObject objectsBack[] = {
        { &main_obj, mainStepsBack,  1 },
        { &cube1,    cube1StepsBack, 1 },
        { &cube2,    cube2StepsBack, 1 },
    };
    Scene sceneBack = { objectsBack, 3 };

    while (true)
    {
        // Forward: rotate + translate
        mainSteps[0]  = calcStep((AnimationDef){ animRotateY,        360.0, 2000 }, fps);
        mainSteps[1]  = calcStep((AnimationDef){ translateObjectByY,   0.3, 2000 }, fps);
        cube1Steps[0] = calcStep((AnimationDef){ animRotateY,       -360.0, 2000 }, fps);
        cube1Steps[1] = calcStep((AnimationDef){ translateObjectByY,  -0.3, 2000 }, fps);
        cube2Steps[0] = calcStep((AnimationDef){ animRotateY,       -360.0, 2000 }, fps);
        cube2Steps[1] = calcStep((AnimationDef){ translateObjectByY,  -0.3, 2000 }, fps);

        runScene(&scene, fps, matrix, width, height, true);

        // Back: translate all objects back to their original Y position
        mainStepsBack[0]  = calcStep((AnimationDef){ translateObjectByY,  -0.3, 1000 }, fps);
        cube1StepsBack[0] = calcStep((AnimationDef){ translateObjectByY,   0.3, 1000 }, fps);
        cube2StepsBack[0] = calcStep((AnimationDef){ translateObjectByY,   0.3, 1000 }, fps);

        runScene(&sceneBack, fps, matrix, width, height, true);
    }

    // Cleanup (unreachable in this demo, but correct practice)
    for (i = 0; i < height; i++)
        free(matrix[i]);
    free(matrix);
    freeObject(&main_obj);
    freeObject(&cube1);
    freeObject(&cube2);
    return 0;
}