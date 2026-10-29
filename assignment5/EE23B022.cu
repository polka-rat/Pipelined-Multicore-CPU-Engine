#include <fstream>
#include <vector>
#include <cuda_runtime.h>

using namespace std;

static const int BLOCK_SIZE = 256;

void readInput(const char *path, int &N, int &K,
               vector<int> &x, vector<int> &y,
               vector<int> &cx, vector<int> &cy)
{
    ifstream in(path);
    in >> N >> K;
    x.resize(N);
    y.resize(N);
    cx.resize(K);
    cy.resize(K);
    for (int i = 0; i < N; ++i)
        in >> x[i];
    for (int i = 0; i < N; ++i)
        in >> y[i];
    for (int j = 0; j < K; ++j)
        in >> cx[j];
    for (int j = 0; j < K; ++j)
        in >> cy[j];
}

void writeOutput(const char *path, const vector<int> &cluster,
                 const vector<int> &cx, const vector<int> &cy)
{
    ofstream out(path);
    for (int i = 0; i < (int)cluster.size(); ++i)
        out << (i ? " " : "") << cluster[i];
    out << '\n';
    for (int j = 0; j < (int)cx.size(); ++j)
        out << cx[j] << ' ' << cy[j] << '\n';
}

__device__ int roundMean(int sum, int count)
{
    int num = 2 * sum - count;
    int den = 2 * count;
    return num < 0 ? num / den : (num + den - 1) / den;
}

__device__ int squaredDistance(int x1,int y1,int x2,int y2){
    int dx=x2-x1;
    int dy=y2-y1;
    return dx*dx+dy*dy;
}

__global__ void assignPoints(const int *x, const int *y,
                             const int *cx, const int *cy,
                             int *cluster, int N, int K)
{
    // Assign each point to its nearest centroid by smallest index.
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < N) {
        int minDist=squaredDistance(x[i],y[i],cx[0],cy[0]);
        cluster[i]=0;

        for(int j=1;j<K;j++){
            int curDist=squaredDistance(x[i],y[i],cx[j],cy[j]);
            if(curDist<minDist){
                minDist=curDist;
                cluster[i]=j;
            }
        }
    }

}

__global__ void accumulateTotals(const int *x, const int *y, const int *cluster,
                           int *sumX, int *sumY, int *count, int N)
{
    // Accumulate sum_x, sum_y, and count for each assigned cluster.
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < N) {
        const int c=cluster[i];
        atomicAdd(&sumX[c], x[i]);
        atomicAdd(&sumY[c], y[i]);
        atomicAdd(&count[c], 1);
    }

}

__global__ void updateCentroids(int *cx, int *cy,
                                const int *sumX, const int *sumY,
                                const int *count, int K)
{
    // Update non-empty centroids with roundMean; leave empty unchanged.
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    if (j < K) {
        if(count[j]>0){
            cx[j]=roundMean(sumX[j],count[j]);
            cy[j]=roundMean(sumY[j],count[j]);
        }
    }
}

int main(int, char **argv)
{
    int N, K;
    vector<int> x, y, cx, cy, cluster;
    readInput(argv[1], N, K, x, y, cx, cy);
    cluster.resize(N);
    vector<int> sumX(K, 0), sumY(K, 0), count(K, 0);

    // Allocate device arrays for points, centroids, and totals.
    int *d_x, *d_y, *d_cx, *d_cy, *d_cluster;
    int *d_sumX, *d_sumY, *d_count;

    cudaMalloc(&d_x, N * sizeof(int));
    cudaMalloc(&d_y, N * sizeof(int));
    cudaMalloc(&d_cx, K * sizeof(int));
    cudaMalloc(&d_cy, K * sizeof(int));
    cudaMalloc(&d_cluster, N * sizeof(int));
    cudaMalloc(&d_sumX, K * sizeof(int));
    cudaMalloc(&d_sumY, K * sizeof(int));
    cudaMalloc(&d_count, K * sizeof(int));

    // Copy the input points and initial centroids to the device.
    cudaMemcpy(d_x, x.data(), N * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_y, y.data(), N * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_cx, cx.data(), K * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_cy, cy.data(), K * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_cluster, cluster.data(), N * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_sumX, sumX.data(), K * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_sumY, sumY.data(), K * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_count, count.data(), K * sizeof(int), cudaMemcpyHostToDevice);

    // Repeat assign, accumulate, and update until centroids converge.
    while (true) {
        int pointBlocks = (N + BLOCK_SIZE - 1) / BLOCK_SIZE;
        int centroidBlocks = (K + BLOCK_SIZE - 1) / BLOCK_SIZE;
        
        cudaMemset(d_sumX, 0, K * sizeof(int));
        cudaMemset(d_sumY, 0, K * sizeof(int));
        cudaMemset(d_count, 0, K * sizeof(int));

        assignPoints<<<pointBlocks, BLOCK_SIZE>>>(d_x, d_y, d_cx, d_cy, d_cluster, N, K);
        accumulateTotals<<<pointBlocks, BLOCK_SIZE>>>(d_x, d_y, d_cluster, d_sumX, d_sumY, d_count, N);
        
        vector<int> temp_cx=cx;
        vector<int> temp_cy=cy;
        updateCentroids<<<centroidBlocks, BLOCK_SIZE>>>(d_cx, d_cy, d_sumX, d_sumY, d_count, K);
        cudaMemcpy(cx.data(), d_cx, K * sizeof(int), cudaMemcpyDeviceToHost);
        cudaMemcpy(cy.data(), d_cy, K * sizeof(int), cudaMemcpyDeviceToHost);

        // Checking if the centroids have changed.
        bool changed = false;
        for(int j=0;j<K;j++){
            if(temp_cx[j]!=cx[j] || temp_cy[j]!=cy[j]){
                changed=true;
                break;
            }
        }
        // Algorithm has converged so process can be stopped.
        if(!changed){
            break;
        }
    }
    // Copy the final cluster assignment indices back to the host.
    cudaMemcpy(cluster.data(), d_cluster, N * sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(cx.data(), d_cx, K * sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(cy.data(), d_cy, K * sizeof(int), cudaMemcpyDeviceToHost);
    writeOutput(argv[2], cluster, cx, cy);

    // Free every device array allocated during the computation.
    cudaFree(d_x);
    cudaFree(d_y);
    cudaFree(d_cx);
    cudaFree(d_cy);
    cudaFree(d_cluster);
    cudaFree(d_sumX);
    cudaFree(d_sumY);
    cudaFree(d_count);

    return 0;
}
