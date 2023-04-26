#include <stdio.h>

int main(int argc, char* argv[]) {
    int a[64], b[64], c[64];
    for(int i=0;i<64;++i)
    {
        a[i] = i+1;
        b[i] = i+1;
    }

    for(int i=0;i<64;i+=4){
        Sum_vect(c,a,b,i);
    }

    for(int i=0;i<10;++i){
        printf("%d: %d",i,c[i]);
    }

    return 0;

    // c = Sum(a, b);
    // printf("%d, %d, %d\n", a, b, c);

    // int arr[3], d;
    // arr[0] = 2;
    // arr[1] = 3;
    // arr[2] = 4;
    // d = Sum(b + c, arr[2]);
    // printf("%d, %d, %d\n", b + c, arr[2], d);
}
