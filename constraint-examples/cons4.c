int a, b, c, d;

void _CONSTRAINT()
{
    (a < b || c < d || a < d) && (b < c || d < a);
}