int _LENGTH(void* ptr);
void GAUSSIAN(double VAR, double mu, double sigma);

typedef struct {
    int a;
    int b;
    double c;
    int* d;
} S1;

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Rect;

int a;
int b[3];
struct S1 s[2];

Point points[5];
Rect rects[5];

void _CONSTRAINT()
{
    a > 5 && a < 10;
    b[1] > b[2] || b[0] < b[1];
    a + b[0] != s[0].a;
    _LENGTH(s[0].d) > 6 && _LENGTH(s[0].d) < 10;
    _LENGTH(s[1].d) > 6 && _LENGTH(s[1].d) < 10;
    s[0].d[6] + s[1].d[6] == s[0].b * s[1].b;
    GAUSSIAN(s[0].c, 1.0, 1.0);

    points[0].x < points[1].x;
    points[0].y < points[1].y;
    points[1].x < points[2].x;
    points[1].y < points[2].y;
    points[2].x < points[3].x;
    points[2].y < points[3].y;
    points[3].x < points[4].x;
    points[3].y > points[4].y;

    rects[0].x < points[4].x && rects[0].y < points[4].y;
    points[4].x < rects[0].x + rects[0].w && points[4].y < rects[0].y + rects[0].h;

    rects[1].x > 3;
    rects[1].y > 3;
    rects[2].x > 3;
    rects[2].y > 3;
    rects[3].x > 3;
    rects[3].y > 3;
    rects[4].x == rects[3].x + rects[3].w;
    rects[4].y == rects[3].y + rects[3].h;
}