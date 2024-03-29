#include <windows.h>
#include <gl/gl.h>

#include "win32.c"
#include "opengl/glFunctions.c"
#include "opengl/openglProgram.c"
#include "font.c"
#include "ui.c"
#include "string.c"
#include "editor.c"

float SYSTEM_SCALE = 1;
#define PX(val) ((val) * SYSTEM_SCALE)

#define HexColor(hex) {(float)((hex >> 16) & 0xff) / 255.0f, \
                       (float)((hex >>  8) & 0xff) / 255.0f, \
                       (float)((hex >>  0) & 0xff) / 255.0f}
#define IsKeyPressed(key) (GetKeyState(key) & 0b10000000)

i32 isRunning = 1;
V2i clientAreaSize = {0};

Layout mainLayout = {0};

f32 zDeltaThisFrame;

#define PATH "..\\string.c"

i32 isEditMode = 0;
StringBuffer file;

f32 padding;
f32 spaceForLineNumbers;
f32 lineNumberToCode;

void Draw();

void OnResize()
{
    glViewport(0, 0, clientAreaSize.x, clientAreaSize.y);

    mainLayout.height = clientAreaSize.y;
    mainLayout.width = clientAreaSize.x;
}

LRESULT OnEvent(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY)
    {
        isRunning = 0;
    }
    else if (message == WM_SIZE)
    {
        clientAreaSize.x = LOWORD(lParam);
        clientAreaSize.y = HIWORD(lParam);

        HDC dc = GetDC(window);
        SYSTEM_SCALE = (float)GetDeviceCaps(dc, LOGPIXELSY) / (float)USER_DEFAULT_SCREEN_DPI;

        padding = PX(15.0f);
        spaceForLineNumbers = PX(40);
        lineNumberToCode = PX(10);

        OnResize();
        InvalidateRect(window, NULL, TRUE);
    }
    else if (message == WM_PAINT)
    {
        PAINTSTRUCT paint = {0};
        HDC dc = BeginPaint(window, &paint);
        Draw();
        SwapBuffers(dc);
        EndPaint(window, &paint);
    }
    else if (message == WM_KEYDOWN)
    {
        if(wParam == VK_DOWN)
            MoveCursor(&file, Down, IsKeyPressed(VK_SHIFT));
        else if(wParam == VK_UP)
            MoveCursor(&file, Up, IsKeyPressed(VK_SHIFT));
        else if(wParam == VK_LEFT && IsKeyPressed(VK_CONTROL))
            MoveCursor(&file, WordJumpLeft, IsKeyPressed(VK_SHIFT));
        else if(wParam == VK_LEFT)
            MoveCursor(&file, Left, IsKeyPressed(VK_SHIFT));
        else if(wParam == VK_RIGHT && IsKeyPressed(VK_CONTROL))
            MoveCursor(&file, WordJumpRight, IsKeyPressed(VK_SHIFT));
        else if(wParam == VK_RIGHT)
            MoveCursor(&file, Right, IsKeyPressed(VK_SHIFT));
        else if(wParam == VK_END)
            MoveCursor(&file, LineEnd, IsKeyPressed(VK_SHIFT));
        else if(wParam == VK_HOME)
            MoveCursor(&file, LineStart, IsKeyPressed(VK_SHIFT));            
        else if (wParam == VK_BACK)
            RemoveCharFromLeft(&file);
        else if (wParam == VK_DELETE)
            RemoveCurrentChar(&file);
        else if (wParam == 'S' && IsKeyPressed(VK_CONTROL))
            WriteMyFile(PATH, file.content, file.size);

        f32 rowHeight = currentFont->textMetric.tmHeight;
        f32 cursorPos = cursor.row * rowHeight + padding;

        if(cursorPos + mainLayout.offsetY + rowHeight * 5 > clientAreaSize.y)
            mainLayout.offsetY = -(cursorPos - clientAreaSize.y + rowHeight * 5);

        if(cursorPos - rowHeight * 5 < -mainLayout.offsetY)
            mainLayout.offsetY = -(cursorPos - rowHeight * 5);

    }
    else if (message == WM_KEYUP)
    {
    }
    else if (message == WM_MOUSEWHEEL)
    {
        zDeltaThisFrame += (float)GET_WHEEL_DELTA_WPARAM(wParam);
    }
    else if (message == WM_CHAR)
    {
        if(wParam >= ' ' || wParam == '\r')
            InsertChartUnderCursor(&file, wParam);
    }

    return DefWindowProc(window, message, wParam, lParam);
}


// https://github.com/ayu-theme/ayu-vim/blob/master/colors/ayu.vim
// V3f bgColor      = HexColor(0x0F1419);
V3f bgColor           = HexColor(0x030303);

V3f cursorColor       = HexColor(0xA011A0);
V3f selectionColor    = HexColor(0x601160);
V3f textColor         = HexColor(0xDDDDDD);
V3f lineNumberColor   = HexColor(0x666666);
V3f selectedTextColor = HexColor(0xEEEEEE);

V3f macroColor        = HexColor(0xC686BD);
V3f keywordColor      = HexColor(0x5E99D2);
V3f stringColor       = HexColor(0xCC937B);

GLuint vertexBuffer;
GLuint vertexArray;
GLuint primitivesProgram;
GLuint textProgram;

#define FLOATS_PER_VERTEX 4
float vertices[] = {
    //Position      UV coords
    1.0f, 0.0f,    1.0f, 0.0f,
    0.0f, 0.0f,    0.0f, 0.0f,
    1.0f, 1.0f,    1.0f, 1.0f,
    0.0f, 1.0f,    0.0f, 1.0f
};

void FormatNumber(i32 val, char *buff)
{
    while(val != 0) 
    {
        *buff = '0' + val % 10;
        val /= 10;
        buff++;
    }
    *buff = '\0';
    ReverseString(buff);
}

void DrawTextBottomRight(float x, float y, char *text, V3f color)
{
    while(*text)
    {
        MyBitmap bitmap2 = currentFont->textures[*text];
        x -= bitmap2.width;
        Mat4 view2 = CreateViewMatrix(x, y, bitmap2.width, bitmap2.height);

        SetMat4("view", CreateViewMatrix(x, y, bitmap2.width, bitmap2.height));
        SetV3f("color", color);
        glBindTexture(GL_TEXTURE_2D, currentFont->cachedTextures[*text]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, ArrayLength(vertices) / FLOATS_PER_VERTEX);

        text++;
    }
}

void DrawSquareAt(i32 row, i32 col, V3f color)
{
    Mat4 cursorView = CreateViewMatrix(
        /*x*/ spaceForLineNumbers + col * currentFont->textures['W'].width,
        /*y*/ mainLayout.height - padding - (row + 1) * currentFont->textMetric.tmHeight - mainLayout.offsetY,
        /*w*/ currentFont->textures['W'].width,
        /*h*/ currentFont->textMetric.tmHeight
    );
    SetV3f("color", color);
    SetMat4("view", cursorView);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, ArrayLength(vertices) / FLOATS_PER_VERTEX);
}

void DrawCursor(StringBuffer *buffer)
{

    if(cursor.selectionStart > -1)
    {
        i32 from = MinI32(cursor.selectionStart, cursor.cursorIndex);
        i32 to   = MaxI32(cursor.selectionStart, cursor.cursorIndex);
        
        i32 row = GetRowAtPosition(buffer, from);
        i32 col = GetColAtPosition(buffer, from);

        for(int i = from; i <= to; i++)
        {
            DrawSquareAt(row, col, selectionColor);
            
            if(*(buffer->content + i) == '\n')
            {
                row++;
                col = 0;
            }
            else 
                col++;
        }
    }

    DrawSquareAt(cursor.row, cursor.col, cursorColor);
}

void Draw()
{
    if(mainLayout.pageHeight > mainLayout.height)
        mainLayout.offsetY = Clampf32(mainLayout.offsetY + zDeltaThisFrame, -(mainLayout.pageHeight - mainLayout.height), 0);

    zDeltaThisFrame = 0;

    
    // allows me to set vecrtex coords as 0..width/height, instead of -1..+1
    // 0,0 is bottom left, not top left
    // matrix in code != matrix in math notation, details at https://youtu.be/kBuaCqaCYwE?t=3084
    // in short: rows in math are columns in code
    Mat4 projection = CreateViewMatrix(-1, -1, 2.0f / (f32)clientAreaSize.x, 2.0f / (f32)clientAreaSize.y);
    
    currentFont = &codeFont;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   
    UseProgram(primitivesProgram);
    SetMat4("projection", projection);
    DrawCursor(&file);

    UseProgram(textProgram);
    SetMat4("projection", projection);
    SetV3f("color", textColor);


    f32 startY = clientAreaSize.y - currentFont->textMetric.tmHeight - padding;

    f32 runningX = spaceForLineNumbers;
    f32 runningY = startY - mainLayout.offsetY;
    i32 currentRow = 0;
    i32 currentCol = 0;
    u8 lineNumberBuff[26];

    for(i32 i = 0; i < file.size; i++)
    {
        u8 code = *(file.content + i);
        u8 nextCode = *(file.content + i + 1);

        if(code == '\n')
        {
            FormatNumber(currentRow + 1, lineNumberBuff);
            DrawTextBottomRight(spaceForLineNumbers - lineNumberToCode, runningY, lineNumberBuff, lineNumberColor);

            currentRow += 1;
            currentCol = 0;
            runningX = spaceForLineNumbers;
            runningY -= currentFont->textMetric.tmHeight;
        }
        else if(code >= ' ') 
        {

            MyBitmap bitmap = currentFont->textures[code];
            
            SetMat4("view", CreateViewMatrix(runningX, runningY, bitmap.width, bitmap.height));
            if (i == cursor.cursorIndex)
                SetV3f("color", selectedTextColor);
            else
                SetV3f("color", textColor);

            glBindTexture(GL_TEXTURE_2D, currentFont->cachedTextures[code]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, ArrayLength(vertices) / FLOATS_PER_VERTEX);

            //TODO monospaced fonts have no kerning, I can figure if font is monospaced and skip kerning lookup
            runningX += bitmap.width + GetKerningValue(code, nextCode);
            currentCol += 1;
        }
    }

    FormatNumber(currentRow + 1, lineNumberBuff);
    DrawTextBottomRight(spaceForLineNumbers - lineNumberToCode, runningY, lineNumberBuff, lineNumberColor);
    
    runningY -= (currentFont->textMetric.tmHeight + padding);

    mainLayout.pageHeight = startY - runningY - mainLayout.offsetY + padding;

    UseProgram(primitivesProgram);
    Mat4 scrollView = DrawScrollbar(&mainLayout, PX(10));
    SetMat4("projection", projection);
    SetV3f("color", (V3f){0.3f, 0.3f, 0.3f});
    SetMat4("view", scrollView);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, ArrayLength(vertices) / FLOATS_PER_VERTEX);
}


void WinMainCRTStartup()
{
    PreventWindowsDPIScaling();

    HINSTANCE instance = GetModuleHandle(0);
    HWND window = OpenWindow(instance, OnEvent);

    HDC dc = GetDC(window);
    Win32InitOpenGL(window);
    InitFunctions();
    InitFonts();

    file = ReadFileIntoDoubledSizedBuffer(PATH);
    textProgram = CreateProgram("..\\shaders\\base.vs", "..\\shaders\\base.fs");
    primitivesProgram = CreateProgram("..\\shaders\\primitives.vs", "..\\shaders\\primitives.fs");

    glGenBuffers(1, &vertexBuffer);
    glGenVertexArrays(1, &vertexArray);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindVertexArray(vertexArray);
    size_t stride = FLOATS_PER_VERTEX * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);  

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);  
    
    glClearColor(bgColor.r, bgColor.g, bgColor.b, 1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (isRunning)
    {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }


        Draw();
        SwapBuffers(dc);

    }

    ExitProcess(0);
}
