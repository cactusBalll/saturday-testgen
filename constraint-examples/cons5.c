typedef struct{
    int x;
    int y;
}Point;

Point p1,p2;

void _CONSTRAINT()
{
    p1.x < p2.x && p1.y < p2.y;
    (p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y) > 255;
}