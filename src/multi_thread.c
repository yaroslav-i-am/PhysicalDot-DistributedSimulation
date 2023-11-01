#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <float.h>
#include <string.h>


#define DT 0.05
#define EPS 10e-32
#define VEC_LEN_EPS 10e-6

typedef struct
{
    double x, y;
} vector;

int bodies, timeSteps;
double *masses, GravConstant;
vector **acc_table;
vector *positions, *velocities, *accelerations;

int thread_count = 0;



vector addVectors(vector a, vector b)
{
    vector c = {a.x + b.x, a.y + b.y};

    return c;
}

vector scaleVector(double b, vector a)
{
    vector c = {b * a.x, b * a.y};

    return c;
}

vector subtractVectors(vector a, vector b)
{
    vector c = {a.x - b.x, a.y - b.y};

    return c;
}

double mod(vector a)
{
    return sqrt(a.x * a.x + a.y * a.y);
}

void initiateSystem(char *fileName)
{
    long i;
    FILE *fp = fopen(fileName, "r");

    fscanf(fp, "%lf%d%d", &GravConstant, &bodies, &timeSteps);

    masses = (double *)malloc(bodies * sizeof(double));
    positions = (vector *)malloc(bodies * sizeof(vector));
    velocities = (vector *)malloc(bodies * sizeof(vector));
    accelerations = (vector *)malloc(bodies * sizeof(vector));

    acc_table = (vector**)malloc(bodies * sizeof(vector*));
    for (i = 0; i < bodies; ++i){
        acc_table[i] = (vector*)malloc(bodies * sizeof(vector));
        for (long j = 0; j < bodies; ++j){
            acc_table[i][j].x = DBL_MAX;            
        }
    }

    for (i = 0; i < bodies; i++)
    {
        fscanf(fp, "%lf", &masses[i]);
        fscanf(fp, "%lf%lf", &positions[i].x, &positions[i].y);
        fscanf(fp, "%lf%lf", &velocities[i].x, &velocities[i].y);
    }

    fclose(fp);
}

void resolveCollisions()
{
    long i, j;

    for (i = 0; i < bodies - 1; i++)
        for (j = i + 1; j < bodies; j++)
        {
            if ((fabs(positions[i].x - positions[j].x) < EPS) && (fabs(positions[i].y - positions[j].y) < EPS))
            {
                vector temp = velocities[i];
                velocities[i] = velocities[j];
                velocities[j] = temp;
            }
        }
}


// ACCELERATIONS ==============================================================================


void* computeAccelerationsRoutine(void* rank){
    long thread_n = (long) rank;
    long batch_size = bodies / thread_count;
    long start = batch_size * thread_n;
    long finish = start + batch_size;    

    long i, j;

    for (i = start; i < finish; i++)
    {
        for (j = 0; j < bodies; j++)
        {
            if (i != j){
                if (fabs(acc_table[j][i].x - DBL_MAX) < EPS)   // Верхняя часть 
                {
                    double vec_len = mod(subtractVectors(positions[i], positions[j]));
                    vec_len = vec_len < VEC_LEN_EPS ? VEC_LEN_EPS : vec_len;
                    acc_table[i][j] =
                        scaleVector(
                            GravConstant * masses[j] / pow(
                                vec_len, 
                                3), 
                            subtractVectors(positions[j], positions[i]));
                }
                else {
                    acc_table[i][j] = scaleVector(-1, acc_table[j][i]);
                }
            }

        }
    }

    return NULL;
}


void computeAccelerations(){
    pthread_t* thread_handles = NULL;
    thread_handles = malloc(thread_count * sizeof(pthread_t));
    
    for (long i = 0; i < thread_count; ++i ) {
        pthread_create(&thread_handles[i], NULL, computeAccelerationsRoutine, (void*) i);
    }
    for (long i = 0; i < thread_count; ++i) {
        pthread_join(thread_handles[i], NULL);
    }
    free(thread_handles);

    for (long i = 0; i < bodies; ++i)
    {
        accelerations[i].x = 0;
        accelerations[i].y = 0;

        for (long j = 0; j < bodies; ++j)
        {
            if (i != j)
                accelerations[i] = addVectors(accelerations[i], acc_table[i][j]);
        }
    }

}


void* sumAccelerationsRoutine(void* rank){
    long thread_n = (long) rank;
    long batch_size = bodies / thread_count;
    long start = batch_size * thread_n;
    long finish = start + batch_size;    

    long i, j;

    for (i = start; i < finish; i++)
    {
        accelerations[i].x = 0;
        accelerations[i].y = 0;        
        for (j = 0; j < bodies; j++)
        {
            if (i != j)
                accelerations[i] = addVectors(accelerations[i], acc_table[i][j]);
        }
    }

    return NULL;
}



void computeAccelerationsMultiSum(){
    pthread_t* thread_handles = NULL;
    thread_handles = malloc(thread_count * sizeof(pthread_t));
    
    for (long i = 0; i < thread_count; ++i ) {
        pthread_create(&thread_handles[i], NULL, computeAccelerationsRoutine, (void*) i);
    }
    for (long i = 0; i < thread_count; ++i) {
        pthread_join(thread_handles[i], NULL);
    }

    // Other thread usage
    for (long i = 0; i < thread_count; ++i ) {
        pthread_create(&thread_handles[i], NULL, sumAccelerationsRoutine, (void*) i);
    }
    for (long i = 0; i < thread_count; ++i) {
        pthread_join(thread_handles[i], NULL);
    }

    free(thread_handles);
}


// VELOCITIES ==============================================================================

void* computeVelocitiesRoutine(void* rank){
    long thread_n = (long) rank;
    long batch_size = bodies / thread_count;
    long start = batch_size * thread_n;
    long finish = start + batch_size;    

    long i, j;

    for (i = start; i < finish; i++)
    {    
        velocities[i] = addVectors(
            velocities[i], 
            scaleVector(DT, accelerations[i])
        );
    }

    return NULL;
}


void computeVelocitiesMulti()
{
    pthread_t* thread_handles = NULL;
    thread_handles = malloc(thread_count * sizeof(pthread_t));

    for (long i = 0; i < thread_count; ++i ) {
        pthread_create(&thread_handles[i], NULL, computeVelocitiesRoutine, (void*) i);
    }
    for (long i = 0; i < thread_count; ++i) {
        pthread_join(thread_handles[i], NULL);
    }

    free(thread_handles);
}

void computeVelocitiesSingle()
{
    int i;

    for (i = 0; i < bodies; i++)
        velocities[i] = addVectors(
            velocities[i], 
            scaleVector(DT, accelerations[i])
        );
}


// POSITIONS ==============================================================================


void* computePositionsRoutine(void* rank){
    long thread_n = (long) rank;
    long batch_size = bodies / thread_count;
    long start = batch_size * thread_n;
    long finish = start + batch_size;    

    long i, j;

    for (i = start; i < finish; i++)
    {    
        positions[i] = addVectors(positions[i], scaleVector(DT, velocities[i]));
    }

    return NULL;
}

void computePositionsSingle()
{
    int i;

    for (i = 0; i < bodies; i++)
        positions[i] = addVectors(positions[i], scaleVector(DT, velocities[i]));
}

void computePositionsMulti()
{
    pthread_t* thread_handles = NULL;
    thread_handles = malloc(thread_count * sizeof(pthread_t));

    for (long i = 0; i < thread_count; ++i ) {
        pthread_create(&thread_handles[i], NULL, computePositionsRoutine, (void*) i);
    }
    for (long i = 0; i < thread_count; ++i) {
        pthread_join(thread_handles[i], NULL);
    }

    free(thread_handles);
}

void simulate()
{
    computeAccelerationsMultiSum();
    computePositionsMulti();
    computeVelocitiesMulti();
    resolveCollisions();
}

int main(int argC, char *argV[])
{
    int i, j;

    if (argC != 4)
        printf("Usage : %s <file name containing system configuration data>", argV[0]);
    else
    {
        char filename[128];
        strcpy(filename, argV[3]);
        strcat(filename, ".csv");

        FILE *fp = fopen(filename, "w");

        thread_count = strtol(argV[1], NULL, 10);
        initiateSystem(argV[2]);

        fprintf(fp, "Body,mass,x,y,vx,vy\n");
        for (i = 0; i < timeSteps; i++)
        {
            // printf("\nCycle %d\n", i + 1);
            simulate();
            for (j = 0; j < bodies; j++)
                fprintf(fp, "%d,%lf,%lf,%lf,%lf,%lf\n", 
                    j + 1, 
                    masses[j],
                    positions[j].x, 
                    positions[j].y, 
                    velocities[j].x, 
                    velocities[j].y
                );
        }
    }

    return 0;
}
