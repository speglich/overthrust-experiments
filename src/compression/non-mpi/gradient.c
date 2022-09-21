#define _POSIX_C_SOURCE 200809L
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define START_TIMER(S) struct timeval start_ ## S , end_ ## S ; gettimeofday(&start_ ## S , NULL);
#define STOP_TIMER(S,T) gettimeofday(&end_ ## S, NULL); T->S += (double)(end_ ## S .tv_sec-start_ ## S.tv_sec)+(double)(end_ ## S .tv_usec-start_ ## S .tv_usec)/1000000;

#ifndef NDISKS
#define NDISKS 8
#endif

#ifndef RATE
#define RATE 16
#endif

#include "stdlib.h"
#include "math.h"
#include "sys/time.h"
#include "xmmintrin.h"
#include "pmmintrin.h"
#include "omp.h"
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "zfp.h"

struct dataobj
{
  void *restrict data;
  int * size;
  int * npsize;
  int * dsize;
  int * hsize;
  int * hofs;
  int * oofs;
} ;

struct profiler
{
  double section0;
  double section1;
  double section2;
} ;

struct io_profiler
{
  double open;
  double read;
  double decompress;
  double close;
} ;

void open_thread_files(int *files, int *metas, int nthreads)
{

  for(int i=0; i < nthreads; i++)
  {
    int nvme_id = i % NDISKS;
    char name[100];

    sprintf(name, "data/nvme%d/thread_%d.data", nvme_id, i);
    printf("Reading file %s\n", name);

    if ((files[i] = open(name, O_RDONLY,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
    {
        perror("Cannot open output file\n"); exit(1);
    }

    sprintf(name, "data/nvme%d/thread_%d.meta", nvme_id, i);
    printf("Reading file %s\n", name);

    if ((metas[i] = open(name, O_RDONLY,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
    {
        perror("Cannot open output file\n"); exit(1);
    }

  }

  return;
}

void save(int nthreads, struct profiler * timers, struct io_profiler * iop, long int read_size)
{
  printf(">>>>>>>>>>>>>> ZFP REVERSE <<<<<<<<<<<<<<<<<\n");

  printf("Threads %d\n", nthreads);
  printf("Disks %d\n", NDISKS);
  printf("Rate %d\n", RATE);

  printf("[REV] Section0 %.2lf s\n", timers->section0);
  printf("[REV] Section1 %.2lf s\n", timers->section1);
  printf("[REV] Section2 %.2lf s\n", timers->section2);

  printf("[IO] Open %.2lf s\n", iop->open);
  printf("[IO] Read %.2lf s\n", iop->read);
  printf("[IO] Decompress %.2lf s\n", iop->decompress);
  printf("[IO] Close %.2lf s\n", iop->close);

  char name[100];
  sprintf(name, "zfp_%d_rev_disks_%d_threads_%d.csv", RATE, NDISKS, nthreads);

  FILE *fpt;
  fpt = fopen(name, "w");

  fprintf(fpt,"Disks, Threads, Bytes, Rate, [REV] Section0, [REV] Section1, [REV] Section2, [IO] Open, [IO] Read, [IO] Decompress, [IO] Close\n");

  fprintf(fpt,"%d, %d, %ld, %d, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf\n", NDISKS, nthreads, read_size, RATE,
        timers->section0, timers->section1, timers->section2, iop->open, iop->read, iop->decompress, iop->close);

  fclose(fpt);
}

size_t** get_slices_size(int *metas, int *spt, int nthreads) {

  size_t** slices_size = (size_t**) malloc(nthreads * sizeof(size_t *));

  for(int tid=0; tid < nthreads; tid++)
  {
    // Get size of the file
    off_t fsize = lseek(metas[tid], (size_t) 0, SEEK_END);

    // Get number of slices per thread file
    spt[tid] = (int) fsize / sizeof(size_t) - 1;

    // Allocate
    slices_size[tid] = (size_t*) malloc(fsize);

    if (slices_size == NULL) {
      printf("Error to allocate slices\n");
      exit(1);
    }

    // Return to begin of the file
    lseek(metas[tid], 0, SEEK_SET);

    // Read to slices_size buffer
    read(metas[tid], &slices_size[tid][0], fsize);
  }

  return slices_size;
}

int Gradient(struct dataobj *restrict damp_vec, const float dt, struct dataobj *restrict grad_vec, const float o_x, const float o_y, const float o_z, struct dataobj *restrict rec_vec, struct dataobj *restrict rec_coords_vec, struct dataobj *restrict u_vec, struct dataobj *restrict v_vec, struct dataobj *restrict vp_vec, const int x_M, const int x_m, const int y_M, const int y_m, const int z_M, const int z_m, const int p_rec_M, const int p_rec_m, const int time_M, const int time_m, const int x0_blk0_size, const int x1_blk0_size, const int y0_blk0_size, const int y1_blk0_size, const int nthreads, const int nthreads_nonaffine, struct profiler * timers)
{
  float (*restrict damp)[damp_vec->size[1]][damp_vec->size[2]] __attribute__ ((aligned (64))) = (float (*)[damp_vec->size[1]][damp_vec->size[2]]) damp_vec->data;
  float (*restrict grad)[grad_vec->size[1]][grad_vec->size[2]] __attribute__ ((aligned (64))) = (float (*)[grad_vec->size[1]][grad_vec->size[2]]) grad_vec->data;
  float (*restrict rec)[rec_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[rec_vec->size[1]]) rec_vec->data;
  float (*restrict rec_coords)[rec_coords_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[rec_coords_vec->size[1]]) rec_coords_vec->data;
  float (*restrict u)[u_vec->size[1]][u_vec->size[2]][u_vec->size[3]] __attribute__ ((aligned (64))) = (float (*)[u_vec->size[1]][u_vec->size[2]][u_vec->size[3]]) u_vec->data;
  float (*restrict v)[v_vec->size[1]][v_vec->size[2]][v_vec->size[3]] __attribute__ ((aligned (64))) = (float (*)[v_vec->size[1]][v_vec->size[2]][v_vec->size[3]]) v_vec->data;
  float (*restrict vp)[vp_vec->size[1]][vp_vec->size[2]] __attribute__ ((aligned (64))) = (float (*)[vp_vec->size[1]][vp_vec->size[2]]) vp_vec->data;

  /* Flush denormal numbers to zero in hardware */
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);

  float r0 = 1.0F/(dt*dt);
  float r1 = 1.0F/dt;

  struct io_profiler * iop = malloc(sizeof(struct io_profiler));

  iop->open = 0;
  iop->read = 0;
  iop->decompress = 0;
  iop->close = 0;

  long int read_size =  0;

  /* Begin open files Section */
  START_TIMER(open)

  int *files = malloc(nthreads * sizeof(int));
  int *metas = malloc(nthreads * sizeof(int));
  int *spt = malloc(nthreads * sizeof(int)); // slices per thread

  off_t *offset = malloc(nthreads * sizeof(off_t));

  if (files == NULL || metas == NULL || spt == NULL || offset == NULL)
  {
      printf("Error to alloc\n");
      exit(1);
  }

  open_thread_files(files, metas, nthreads);

  size_t **slices_size = get_slices_size(metas, spt, nthreads);

  for (int tid=0; tid < nthreads; tid++)
  {
    offset[tid] = 0;
  }

  for (int tid=0; tid < nthreads; tid++)
  {
    close(metas[tid]);
  }

  STOP_TIMER(open, iop)
  /* End open files section */

  size_t u_size = u_vec->size[2]*u_vec->size[3]*sizeof(float);

  for (int time = time_M, t0 = (time)%(3), t1 = (time + 2)%(3), t2 = (time + 1)%(3); time >= time_m; time -= 1, t0 = (time)%(3), t1 = (time + 2)%(3), t2 = (time + 1)%(3))
  {
    /* Begin section0 */
    START_TIMER(section0)
    #pragma omp parallel num_threads(nthreads)
    {
      #pragma omp for collapse(2) schedule(dynamic,1)
      for (int x0_blk0 = x_m; x0_blk0 <= x_M; x0_blk0 += x0_blk0_size)
      {
        for (int y0_blk0 = y_m; y0_blk0 <= y_M; y0_blk0 += y0_blk0_size)
        {
          for (int x = x0_blk0; x <= MIN(x0_blk0 + x0_blk0_size - 1, x_M); x += 1)
          {
            for (int y = y0_blk0; y <= MIN(y0_blk0 + y0_blk0_size - 1, y_M); y += 1)
            {
              #pragma omp simd aligned(damp,v,vp:64)
              for (int z = z_m; z <= z_M; z += 1)
              {
                float r10 = 1.0F/(vp[x + 6][y + 6][z + 6]*vp[x + 6][y + 6][z + 6]);
                v[t1][x + 6][y + 6][z + 6] = (r1*damp[x + 1][y + 1][z + 1]*v[t0][x + 6][y + 6][z + 6] + r10*(-r0*(-2.0F*v[t0][x + 6][y + 6][z + 6]) - r0*v[t2][x + 6][y + 6][z + 6]) + 1.77777773e-5F*(v[t0][x + 3][y + 6][z + 6] + v[t0][x + 6][y + 3][z + 6] + v[t0][x + 6][y + 6][z + 3] + v[t0][x + 6][y + 6][z + 9] + v[t0][x + 6][y + 9][z + 6] + v[t0][x + 9][y + 6][z + 6]) + 2.39999994e-4F*(-v[t0][x + 4][y + 6][z + 6] - v[t0][x + 6][y + 4][z + 6] - v[t0][x + 6][y + 6][z + 4] - v[t0][x + 6][y + 6][z + 8] - v[t0][x + 6][y + 8][z + 6] - v[t0][x + 8][y + 6][z + 6]) + 2.39999994e-3F*(v[t0][x + 5][y + 6][z + 6] + v[t0][x + 6][y + 5][z + 6] + v[t0][x + 6][y + 6][z + 5] + v[t0][x + 6][y + 6][z + 7] + v[t0][x + 6][y + 7][z + 6] + v[t0][x + 7][y + 6][z + 6]) - 1.30666663e-2F*v[t0][x + 6][y + 6][z + 6])/(r0*r10 + r1*damp[x + 1][y + 1][z + 1]);
              }
            }
          }
        }
      }
    }
    STOP_TIMER(section0,timers)
    /* End section0 */

    /* Begin section1 */
    START_TIMER(section1)
    #pragma omp parallel num_threads(nthreads_nonaffine)
    {
      int chunk_size = (int)(fmax(1, (1.0F/3.0F)*(p_rec_M - p_rec_m + 1)/nthreads_nonaffine));
      #pragma omp for collapse(1) schedule(dynamic,chunk_size)
      for (int p_rec = p_rec_m; p_rec <= p_rec_M; p_rec += 1)
      {
        float posx = -o_x + rec_coords[p_rec][0];
        float posy = -o_y + rec_coords[p_rec][1];
        float posz = -o_z + rec_coords[p_rec][2];
        int ii_rec_0 = (int)(floor(4.0e-2F*posx));
        int ii_rec_1 = (int)(floor(4.0e-2F*posy));
        int ii_rec_2 = (int)(floor(4.0e-2F*posz));
        int ii_rec_3 = 1 + (int)(floor(4.0e-2F*posz));
        int ii_rec_4 = 1 + (int)(floor(4.0e-2F*posy));
        int ii_rec_5 = 1 + (int)(floor(4.0e-2F*posx));
        float px = (float)(posx - 2.5e+1F*(int)(floor(4.0e-2F*posx)));
        float py = (float)(posy - 2.5e+1F*(int)(floor(4.0e-2F*posy)));
        float pz = (float)(posz - 2.5e+1F*(int)(floor(4.0e-2F*posz)));
        if (ii_rec_0 >= x_m - 1 && ii_rec_1 >= y_m - 1 && ii_rec_2 >= z_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_1 <= y_M + 1 && ii_rec_2 <= z_M + 1)
        {
          float r2 = (dt*dt)*(vp[ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_2 + 6]*vp[ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_2 + 6])*(-6.4e-5F*px*py*pz + 1.6e-3F*px*py + 1.6e-3F*px*pz - 4.0e-2F*px + 1.6e-3F*py*pz - 4.0e-2F*py - 4.0e-2F*pz + 1)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_2 + 6] += r2;
        }
        if (ii_rec_0 >= x_m - 1 && ii_rec_1 >= y_m - 1 && ii_rec_3 >= z_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_1 <= y_M + 1 && ii_rec_3 <= z_M + 1)
        {
          float r3 = (dt*dt)*(vp[ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_3 + 6]*vp[ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_3 + 6])*(6.4e-5F*px*py*pz - 1.6e-3F*px*pz - 1.6e-3F*py*pz + 4.0e-2F*pz)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_3 + 6] += r3;
        }
        if (ii_rec_0 >= x_m - 1 && ii_rec_2 >= z_m - 1 && ii_rec_4 >= y_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_2 <= z_M + 1 && ii_rec_4 <= y_M + 1)
        {
          float r4 = (dt*dt)*(vp[ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_2 + 6]*vp[ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_2 + 6])*(6.4e-5F*px*py*pz - 1.6e-3F*px*py - 1.6e-3F*py*pz + 4.0e-2F*py)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_2 + 6] += r4;
        }
        if (ii_rec_0 >= x_m - 1 && ii_rec_3 >= z_m - 1 && ii_rec_4 >= y_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_3 <= z_M + 1 && ii_rec_4 <= y_M + 1)
        {
          float r5 = (dt*dt)*(vp[ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_3 + 6]*vp[ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_3 + 6])*(-6.4e-5F*px*py*pz + 1.6e-3F*py*pz)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_3 + 6] += r5;
        }
        if (ii_rec_1 >= y_m - 1 && ii_rec_2 >= z_m - 1 && ii_rec_5 >= x_m - 1 && ii_rec_1 <= y_M + 1 && ii_rec_2 <= z_M + 1 && ii_rec_5 <= x_M + 1)
        {
          float r6 = (dt*dt)*(vp[ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_2 + 6]*vp[ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_2 + 6])*(6.4e-5F*px*py*pz - 1.6e-3F*px*py - 1.6e-3F*px*pz + 4.0e-2F*px)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_2 + 6] += r6;
        }
        if (ii_rec_1 >= y_m - 1 && ii_rec_3 >= z_m - 1 && ii_rec_5 >= x_m - 1 && ii_rec_1 <= y_M + 1 && ii_rec_3 <= z_M + 1 && ii_rec_5 <= x_M + 1)
        {
          float r7 = (dt*dt)*(vp[ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_3 + 6]*vp[ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_3 + 6])*(-6.4e-5F*px*py*pz + 1.6e-3F*px*pz)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_3 + 6] += r7;
        }
        if (ii_rec_2 >= z_m - 1 && ii_rec_4 >= y_m - 1 && ii_rec_5 >= x_m - 1 && ii_rec_2 <= z_M + 1 && ii_rec_4 <= y_M + 1 && ii_rec_5 <= x_M + 1)
        {
          float r8 = (dt*dt)*(vp[ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_2 + 6]*vp[ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_2 + 6])*(-6.4e-5F*px*py*pz + 1.6e-3F*px*py)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_2 + 6] += r8;
        }
        if (ii_rec_3 >= z_m - 1 && ii_rec_4 >= y_m - 1 && ii_rec_5 >= x_m - 1 && ii_rec_3 <= z_M + 1 && ii_rec_4 <= y_M + 1 && ii_rec_5 <= x_M + 1)
        {
          float r9 = 6.4e-5F*px*py*pz*(dt*dt)*(vp[ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_3 + 6]*vp[ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_3 + 6])*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_3 + 6] += r9;
        }
      }
    }
    STOP_TIMER(section1,timers)
    /* End section1 */

    /* Begin decompress section */
    START_TIMER(decompress)
    #pragma omp parallel for schedule(static,1) num_threads(nthreads)
    for(int i= u_vec->size[1]-1;i>=0;i--)
    {
      int tid = i%nthreads;

      zfp_type type = zfp_type_float;
      zfp_field* field = zfp_field_2d(u[t2][i], type, u_vec->size[2],u_vec->size[3]);

      zfp_stream* zfp = zfp_stream_open(NULL);

      zfp_stream_set_rate(zfp, RATE, type, zfp_field_dimensionality(field), zfp_false);
      //zfp_stream_set_reversible(zfp);
      //zfp_stream_set_precision(zfp, 1e-3);

      off_t bufsize = zfp_stream_maximum_size(zfp, field);
      void* buffer = malloc(bufsize);

      bitstream* stream = stream_open(buffer, bufsize);

      zfp_stream_set_bit_stream(zfp, stream);
      zfp_stream_rewind(zfp);

      int slice = spt[tid];

      offset[tid] += slices_size[tid][slice];

      lseek(files[tid], -1 * offset[tid], SEEK_END);

      int ret = read(files[tid], buffer, slices_size[tid][slice]);
      read_size += slices_size[tid][slice];

      if (ret != slices_size[tid][slice]) {
          printf("%zu\n", offset[tid]);
          perror("Cannot open output file");
          exit(1);
      }

      if (!zfp_decompress(zfp, field)) {
        printf("decompression failed\n");
        exit(1);
      }

      zfp_field_free(field);
      zfp_stream_close(zfp);
      stream_close(stream);
      free(buffer);
      spt[tid]--;
    }

    STOP_TIMER(decompress, iop)
    /* End decompress section */

    /* Begin section2 */
    START_TIMER(section2)
    #pragma omp parallel num_threads(nthreads)
    {
      #pragma omp for collapse(2) schedule(static,1)
      for (int x1_blk0 = x_m; x1_blk0 <= x_M; x1_blk0 += x1_blk0_size)
      {
        for (int y1_blk0 = y_m; y1_blk0 <= y_M; y1_blk0 += y1_blk0_size)
        {
          for (int x = x1_blk0; x <= MIN(x1_blk0 + x1_blk0_size - 1, x_M); x += 1)
          {
            for (int y = y1_blk0; y <= MIN(y1_blk0 + y1_blk0_size - 1, y_M); y += 1)
            {
              #pragma omp simd aligned(grad,u,v:64)
              for (int z = z_m; z <= z_M; z += 1)
              {
                grad[x + 1][y + 1][z + 1] += -(r0*(-2.0F*v[t0][x + 6][y + 6][z + 6]) + r0*v[t1][x + 6][y + 6][z + 6] + r0*v[t2][x + 6][y + 6][z + 6])*u[t0][x + 6][y + 6][z + 6];
              }
            }
          }
        }
      }
    }
    STOP_TIMER(section2,timers)
    /* End section2 */
  }

  /* Begin close section */
  START_TIMER(close)
  for(int i=0; i < nthreads; i++){
    close(files[i]);
  }
  STOP_TIMER(close, iop)
  /* End close section */

  iop->decompress = iop->decompress - iop->read;

  save(nthreads, timers, iop, read_size);

  free(iop);
  free(files);
  free(metas);

  free(offset);
  free(spt);

  for(int tid=0; tid < nthreads; tid++){
    free(slices_size[tid]);
  }

  free(slices_size);

  return 0;
}
