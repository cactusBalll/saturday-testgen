int _LENGTH(void* ptr);
void GAUSSIAN(double mu, double sigma, double VAR);

typedef struct {
    int a;
    int b;
    double c;
    int* d;
} S1;

int a[3][4][5];

void _CONSTRAINT()
{
    a[0][0][1] == a[0][1][2];
    a[0][0][2] == a[0][2][1];
    a[2][0][1] == a[0][1][2];
    a[1][0][3] == a[1][1][2];
    a[1][0][2] <= a[1][1][3];
}