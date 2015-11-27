/// Implementation of the Number-Theoretical Transform in GF(P) Galois field
#include <iostream>
#include <algorithm> 
#include <stdint.h>

#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
#define MY_CPU_AMD64
#endif

#if defined(MY_CPU_AMD64) || defined(_M_IA64)
#define MY_CPU_64BIT
#endif

typedef uint32_t ElementT;        // data items type, 32-bit unsigned integer for GF(P) computations with P>65536
typedef uint64_t DoubleElementT;  // twice wider type to hold intermediate results


template <ElementT P>
ElementT GF_Add (ElementT X, ElementT Y)
{
    ElementT res = X + Y;
    return res - ((res>=P)+(res<X))*P;   // (res>=P || res<X)? res-P : res;
}

template <ElementT P>
ElementT GF_Sub (ElementT X, ElementT Y)
{
    ElementT res = X - Y;
    return res + (res>X)*P;   // res<=X? res : res+P
}

#if 0  
// Alternative GF_Mul64 implementation for GCC - unfortunately, GCC 4.9 generates over-smart code for it

#include <inttypes.h>
typedef unsigned __int128 uint128_t;
typedef uint128_t FourElement;    // 4x wider type to hold intermediate MUL results

template <ElementT P>
ElementT GF_Mul64 (ElementT X, ElementT Y)
{
    DoubleElementT res = DoubleElementT(X)*Y;
    DoubleElementT invP = DoubleElementT((FourElement(1)<<64) / P);
    res -= DoubleElementT(((res)*FourElement(invP)) >> 64) * P;
    return ElementT(res>=P? res-P : res);
}

#elif _MSC_VER
// Alternative GF_Mul64 implementation made with MSVC intrinsics

template <ElementT P>
ElementT GF_Mul64 (ElementT X, ElementT Y)
{
    const DoubleElementT estInvP = ((DoubleElementT(1)<<63) / P) << 1;                  
    const DoubleElementT invP    = (estInvP*P > (estInvP+1)*P? estInvP : estInvP+1);    
    DoubleElementT res = DoubleElementT(X)*Y;
    res -= __umulh(res,invP) * P;
    return ElementT(res>=P? res-P : res);
}

#else

// GF_Mul64 is optimized for 64-bit CPUs
template <ElementT P>
ElementT GF_Mul64 (ElementT X, ElementT Y)
{
    return ElementT( (DoubleElementT(X)*Y) % P);
}

#endif

// GF_Mul32 is optimized for 32-bit CPUs, SIMD and GPUs
template <ElementT P>
ElementT GF_Mul32 (ElementT X, ElementT Y)
{
    // invP32 := (2**64)/P - 2**32  :  if 2**31<P<2**32, then 2**32 < (2**64)/P < 2**33, and invP32 is a 32-bit value
    const DoubleElementT estInvP = ((DoubleElementT(1)<<63) / P) << 1;                          // == invP & (~1)
    const ElementT       invP32  = ElementT(estInvP*P > (estInvP+1)*P? estInvP : estInvP+1);    // we can't use 1<<64 for exact invP computation so we add the posible 1 in other way
    DoubleElementT res = DoubleElementT(X)*Y;
    res  -=  ((res + (res>>32)*invP32) >> 32) * P;    // The same as res -= ((res*invP) >> 64) * P, where invP = (2**64)/P, but optimized for 32-bit computations
    return ElementT(res>=P? res-P : res);
}

#ifdef MY_CPU_64BIT
#define GF_Mul GF_Mul64
#else
#define GF_Mul GF_Mul32
#endif



template <typename T, T P>
void apply (T* data, size_t N, size_t SIZE, T root) 
{
    T root_sqr = GF_Mul<P> (root, root);  // first root of power N of 1
    if (N>1024) {
        #pragma omp task
        apply<T,P> (data,        N/2, SIZE, root_sqr);
        #pragma omp task
        apply<T,P> (data+N*SIZE, N/2, SIZE, root_sqr);
        #pragma omp taskwait
    } else if (N>1) {
        apply<T,P> (data,        N/2, SIZE, root_sqr);
        apply<T,P> (data+N*SIZE, N/2, SIZE, root_sqr);
    }

    T root_i = root;   // first root of power 2N of 1 
    for (size_t i=0; i<N*SIZE; i+=SIZE) {
        for (size_t k=0; k<SIZE; k++) {
            size_t i1 = i+k, i2 = i+k+N*SIZE;
            T temp   = GF_Mul<P> (root_i, data[i2]);
            data[i2] = GF_Sub<P> (data[i1], temp);
            data[i1] = GF_Add<P> (data[i1], temp);
        }
        root_i = GF_Mul<P> (root_i, root);  // next root of power 2N of 1
    }
}
 
template <size_t Exp, size_t SIZE, typename T, T P>
class NTT 
{
    enum { N = 1<<Exp };
public:
    void ntt (T* data) 
    {
        T root = 1557;                      // init 'root' with root of 1 of power 2**20 in GF(0xFFF00001)
        for (int i=20; --i>Exp; )
            root = GF_Mul<P> (root, root);  // find root of 1 of power 2N
        apply<T,P> (data, N, SIZE, root);
    }
};



// Find first root of 1 of power 2**N
template <typename T, T P>
void FindRoot (int N)
{
    for (T i=2; i<P; i++)
    {
        T q = i;
        for (int j=0; j<N; j++)
        {
            if (q==1)  goto next;
            q = GF_Mul<P> (q,q);
        }
        if (q==1)
        { 
            std::cout << i << "\n";
            break;
        }
next:;        
    }
}


// Test the GF_Mul32 correctness
template <ElementT P>
void Test_GF_Mul32()
{
    int n = 0;
    for (ElementT i=P; --i; )
    {
        std::cout << std::hex << "\r" << i << "...";
        for (ElementT j=i; --j; )
            if (GF_Mul64<P> (i,j)  !=  GF_Mul32<P> (i,j))
            {
                std::cout << std::hex << "\r" << i << "*" << j << "=" << GF_Mul64<P> (i,j) << " != " << GF_Mul32<P> (i,j) << "\n" ;
                if (++n>10) return;
            }          
    }
}


int main()
{
    const ElementT P = 0xFFF00001;
    // Test_GF_Mul32<P>(); 
    // FindRoot<ElementT,P>(20);  // prints 1557

    const size_t SIZE = 128<<20;  // 512 MB
    ElementT *data = new ElementT[SIZE];
    for (int i=0; i<SIZE; i++)
        data[i] = i;
    
    NTT<19,SIZE/1048576,ElementT,P> transformer;
    #pragma omp parallel num_threads(16)
    #pragma omp single
    transformer.ntt(data);

    uint32_t sum = 314159253;
    for (int i=0; i<SIZE; i++)
        sum = (sum+data[i])*123456791;
    if (sum != 3267607014UL)
        std::cout << "checksum failed: " << sum << " ";

    return 0;
}
