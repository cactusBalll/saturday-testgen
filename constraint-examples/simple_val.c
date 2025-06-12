int a,b,c,d;

void _CONSTRAINT()
{
    a > 5 && a < 10;
    b > a || b < c;
    a + b > 30;
    d > 30 || d < 10 || b < 6;
}