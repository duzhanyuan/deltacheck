struct S
{
  unsigned int x, y;
} my_s;

int main()
{
  unsigned int *p;
  
  my_s.x=0;
  my_s.y=0;
  
  // read
  p=&my_s.x;
  *p=2;
  my_s.x=1;
  assert(*p==1);
  
  // write
  p=&my_s.y;
  *p=3;
  assert(my_s.y==3);
}
