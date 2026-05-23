# cliRender
Just a little project to fight the boredom. 

renders .obj files or manually defined structures on the command line.

Usage:

compile it like usual:

*gcc -Wall -pedantic demo.c cmdrender.c -o demo -lm*

- demo.exe -w 400 -h 400 -f example.obj
- demo.exe -f example.obj
- demo.exe -w 100 -h 100

If no obj is provided, a cube is displayed. 

Right now, everything runs on the CPU.
I only tested it on windows.

Examples:

<img width="600" height="328" alt="83c2bb52aa0457483ff36cca89a6a21f" src="https://github.com/user-attachments/assets/84dac584-4775-412a-bf73-d90b3ce1eb8f" />
<img width="600" height="416" alt="a69700eb06270154f69ba045de86113f" src="https://github.com/user-attachments/assets/2eecb3d6-68af-47d2-928a-17c6bc57799b" />
<img width="600" height="386" alt="c8a9f01a0f1b137be823a6c4b0ac728f" src="https://github.com/user-attachments/assets/17a3c546-c2c7-43d1-aa53-5efbf18dbf4b" />

A more complex demo with multiple objects is included.
