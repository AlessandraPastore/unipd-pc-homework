#include <iostream>
#include <mpi.h>
#include "matrix.h"
#include "sequential.h"

#define TAG 21

void floydWarhsallSquaredParallel(pc::matrix<double> &dist, pc::matrix<int> &pred, int n, int s, int thread_size, int thread_rank);

void whiteFor(int [], pc::matrix<double> &dist, pc::matrix<int> &pred);

template <class T>
void plusOfSquareToLinear(pc::matrix<T> &matrix, T* dest,  int s, int n, int h, int k);

template <class T>
void plusOfSquareFromLinear(pc::matrix<T> &matrix, T* dest,  int s, int n, int h, int k);

template <class T>
void submatrixToLinear(pc::matrix<T> &m, T* dest, int dx, int dy, int s);

template <class T>
void linearToSubatrix(pc::matrix<T> &m, T* source, int dx, int dy, int s);

int main(int argc , char ** argv) {
    int thread_rank , thread_size ;
    MPI_Init (& argc , & argv );
    MPI_Comm_rank ( MPI_COMM_WORLD , & thread_rank );
    MPI_Comm_size ( MPI_COMM_WORLD , & thread_size );
    printf("Hello world from process %d of %d \n", thread_rank, thread_size);

    pc::matrix<double> dist = pc::matrix<double>(0);
    pc::matrix<int> pred = pc::matrix<int>(0);
    int s = 0;
    int n = 0;

    if(thread_rank == 0) {
        std::cin >> s;

        std::cin >> dist;

        pred = pc::matrix<int>(dist.getSize());

        n = dist.getSize();
    }


    MPI_Barrier( MPI_COMM_WORLD );
    MPI_Bcast( &n, 1, MPI_INT, 0, MPI_COMM_WORLD );
    MPI_Bcast( &s, 1, MPI_INT, 0, MPI_COMM_WORLD );

    floydWarhsallSquaredParallel(dist, pred, n, s,  thread_size, thread_rank);


    printf("Goodbye from process %d of %d \n", thread_rank, thread_size);

    MPI_Barrier( MPI_COMM_WORLD );


    if(thread_rank == 0) {
        std::cout << dist << std::endl; //distances matrix: min costs
        std::cout << pred << std::endl; //predecessors matrix: min path
    }

    MPI_Finalize ();
    return 0;
}

void floydWarhsallSquaredParallel(pc::matrix<double> &dist, pc::matrix<int> &pred, int n, int s,  int thread_size, int thread_rank){
    if(((int)((double)n/(double)s))%2 == 0) {
        if(thread_rank == 0) {
            std::cout << "ERROR! work only with a odd division of matrix" << std::endl;
        }
        return;
    }

    printf("cpu %d load matrix n %d with step %d \n", thread_rank, n,s);


    if(thread_rank == 0) {

        printf("cpu %d is start to initialised the floyd \n", thread_rank);
        // initialised predecessors
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                pred.set(i, j, i);
            }
        }

        //computation of min path costs
        for (int h = 0; h < n; h += s) {
            // dark green self dependent
            floyd(dist, pred, h, h, h, h, h, h, s); // first


            for (int k = 1; k <= (n / s) / 2; k++) { // second - ....
                // light green - row h
                int j = (h - k * s + n) % n;
                floyd(dist, pred, h, j, h, h, h, j, s);

                j = (h + k * s + n) % n;
                floyd(dist, pred, h, j, h, h, h, j, s);

                // light green - row h
                int i = (h - k * s + n) % n;
                floyd(dist, pred, i, h, i, h, h, h, s);

                i = (h + k * s + n) % n;
                floyd(dist, pred, i, h, i, h, h, h, s);

                // white - rest of cells

                int data[] = {s, n, h, k};
                if (thread_size == 1) {
                    whiteFor(data, dist, pred);
                } else {
                    printf("cpu %d is start to initialised to send data at cpu %d \n", thread_rank, k % (thread_size -1) +1);

                    int size = 12*(k+1)*s*s;
                    double * linearSquareDist = new double[size];
                    int * linearSquarePred = new int[size];
                    plusOfSquareToLinear<double>(dist, linearSquareDist, s, n, h, k);
                    plusOfSquareToLinear<int>(pred, linearSquarePred, s, n, h, k);
                    MPI_Send(data, 4, MPI_INT, k % (thread_size -1) +1, TAG, MPI_COMM_WORLD);
                    MPI_Send(linearSquareDist, size, MPI_DOUBLE, k % (thread_size -1) +1, TAG, MPI_COMM_WORLD);
                    MPI_Send(linearSquarePred, size, MPI_INT, k % (thread_size -1) +1, TAG, MPI_COMM_WORLD);
                    delete[] linearSquareDist;
                    delete[] linearSquarePred;
                }
            }

            if (thread_size != 1) {
                MPI_Barrier(MPI_COMM_WORLD);
            }
            //possible negative cycle
            for (int i = 0; i < n; i++) {
                if (dist.get(i, i) < 0) {
                    MPI_Abort(MPI_COMM_WORLD, MPI_ERR_UNKNOWN);

                    throw std::invalid_argument("negative cycle");
                }
            }
        }
    }
    else {
        printf("cpu %d is ready \n", thread_rank);

        int kk = (n / s) / 2;
        for (int h = 0; h < n; h += s) {
            for (int i = 0; i < kk / (thread_size -1); i++) {
                printf("cpu %d is ready to recive packet %d of %d \n", thread_rank, i,  kk / (thread_size -1));

                int data[4];
                MPI_Status status;
                MPI_Recv(data, 4, MPI_INT, MPI_ANY_SOURCE, TAG, MPI_COMM_WORLD, &status);
                int k = data[3];
                int size = 12*(k+1)*s*s;
                double * linearSquareDist = new double[size];
                int * linearSquarePred = new int[size];
                MPI_Recv(linearSquareDist, size, MPI_DOUBLE, MPI_ANY_SOURCE, TAG, MPI_COMM_WORLD, &status);
                MPI_Recv(linearSquarePred, size, MPI_INT, MPI_ANY_SOURCE, TAG, MPI_COMM_WORLD, &status);
                printf("cpu %d recive packet with data s: %d n: %d h: %d k: %d \n", thread_rank, data[0], data[1], data[2], data[3]);

                //whiteFor(data, dist, pred);

                printf("cpu %d end of calc \n", thread_rank);

                delete[] linearSquareDist;
                delete[] linearSquarePred;
            }
            if(thread_rank + 1 < kk % (thread_size -1)) {
                printf("cpu %d is ready to recive last packet \n", thread_rank);

                int data[4];
                MPI_Status status;
                MPI_Recv(data, 4, MPI_INT, MPI_ANY_SOURCE, TAG, MPI_COMM_WORLD, &status);
                int k = data[3];
                int size = 12*(k+1)*s*s;
                double * linearSquareDist = new double[size];
                int * linearSquarePred = new int[size];
                MPI_Recv(linearSquareDist, size, MPI_DOUBLE, MPI_ANY_SOURCE, TAG, MPI_COMM_WORLD, &status);
                MPI_Recv(linearSquarePred, size, MPI_INT, MPI_ANY_SOURCE, TAG, MPI_COMM_WORLD, &status);
                printf("cpu %d recive packet with data s: %d n: %d h: %d k: %d \n", thread_rank, data[0], data[1], data[2], data[3]);


                //whiteFor(data, dist, pred);

                printf("cpu %d end of calc \n", thread_rank);

                delete[] linearSquareDist;
                delete[] linearSquarePred;
            }
            MPI_Barrier ( MPI_COMM_WORLD );
        }
    }
}

void whiteFor(int args[], pc::matrix<double> &dist, pc::matrix<int> &pred) {
    int s = args[0];
    int n = args[1];
    int h = args[2];
    int k = args[3];
    int i,j;
    for(int l = k - (n / s) / 2; l <= (n / s) / 2 + k; l++) {
        int m = (l+k)*s;
        i = (m+n) % n;
        if(i == h) continue;
        j = (h-k*s+n) % n;
        floyd(dist,pred,i,j,i,h,h,j,s);
        j = (h+k*s+n) % n;
        floyd(dist,pred,i,j,i,h,h,j,s);
    }
    for(int l = k-(n/s)/2+s; l<=(n/s)/2+k-s; l++) {
        int m = (l+k)*s;
        j = (m+n) % n;
        if(j == h) continue;
        i = (h-k*s+n) % n;
        floyd(dist,pred,i,j,i,h,h,j,s);
        i = (h+k*s+n) % n;
        floyd(dist,pred,i,j,i,h,h,j,s);
    }
}

template <class T>
void plusOfSquareToLinear(pc::matrix<T> &matrix, T* dest,  int s, int n, int h, int k){
    int a = 0;
    int i,j;
    for(int l = k - (n / s) / 2; l <= (n / s) / 2 + k; l++) {
        int m = (l + k) * s;
        i = (m + n) % n;
        j = (h - k * s + n) % n;
        submatrixToLinear<T>(matrix, dest + a, i, j, s);
        a+= s*s;
        j = (h + n) % n;
        submatrixToLinear<T>(matrix, dest + a, i, j, s);
        a+= s*s;
        j = (h+k*s+n) % n;
        submatrixToLinear<T>(matrix, dest + a, i, j, s);
        a+= s*s;
    }
    for(int l = k-(n/s)/2+s; l<=(n/s)/2+k-s; l++) {
        int m = (l + k) * s;
        j = (m + n) % n;
        i = (h - k * s + n) % n;
        submatrixToLinear<T>(matrix, dest + a, i, j, s);
        a+= s*s;
        i = (h + n) % n;
        submatrixToLinear<T>(matrix, dest + a, i, j, s);
        a+= s*s;
        i = (h+k*s+n) % n;
        submatrixToLinear<T>(matrix, dest + a, i, j, s);
        a+= s*s;
    }
}

template <class T>
void plusOfSquareFromLinear(pc::matrix<T> &matrix, T* dest,  int s, int n, int h, int k) {
    int a = 0;
    int i,j;
    for(int l = k - (n / s) / 2; l <= (n / s) / 2 + k; l++) {
        int m = (l + k) * s;
        i = (m + n) % n;
        j = (h - k * s + n) % n;
        linearToSubatrix<T>(matrix, dest + a, i, j, s);
        a+= s*s;
        j = (h + n) % n;
        linearToSubatrix<T>(matrix, dest + a, i, j, s);
        a+= s*s;
        j = (h+k*s+n) % n;
        linearToSubatrix<T>(matrix, dest + a, i, j, s);
        a+= s*s;
    }
    for(int l = k-(n/s)/2+s; l<=(n/s)/2+k-s; l++) {
        int m = (l + k) * s;
        j = (m + n) % n;
        i = (h - k * s + n) % n;
        linearToSubatrix<T>(matrix, dest + a, i, j, s);
        a+= s*s;
        i = (h + n) % n;
        linearToSubatrix<T>(matrix, dest + a, i, j, s);
        a+= s*s;
        i = (h+k*s+n) % n;
        linearToSubatrix<T>(matrix, dest + a, i, j, s);
        a+= s*s;
    }
}

template <class T>
void submatrixToLinear(pc::matrix<T> &m, T* dest, int dx, int dy, int s) {
    int k = 0;
    for(int i = 0; i< s; i++) {
        for(int j = 0; j<s ; j++, k++) {
            dest[k] = m[i+dx][j+dy];
        }
    }
}

template <class T>
void linearToSubatrix(pc::matrix<T> &m, T* source, int dx, int dy, int s) {
    int k = 0;
    for(int i = 0; i< s; i++) {
        for(int j = 0; j<s ; j++, k++) {
            m[i+dx][j+dy] = source[k];
        }
    }
}