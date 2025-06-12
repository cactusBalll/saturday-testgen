int _LENGTH(void* ptr);
void GAUSSIAN(double VAR, double mu, double sigma);

typedef struct {
    double *a;
    int length;
} S1;

struct S1 s[5];

void _CONSTRAINT()
{

    s[0].length == 5 && 5 == _LENGTH(s[0].a);
    s[1].length == 6 && 6 == _LENGTH(s[1].a);
    s[2].length == 7 && 7 == _LENGTH(s[2].a);
    s[3].length == 8 && 8 == _LENGTH(s[3].a);
    s[4].length == 9 && 9 == _LENGTH(s[4].a);
}