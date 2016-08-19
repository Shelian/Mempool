#pragma once

#include <stdarg.h>

#define __DEBUG__
static string GetFileName(const string& path)
{
	char ch='/';

#ifdef _WIN32
	ch='\\';
#endif

	size_t pos = path.rfind(ch);
	if(pos==string::npos)
		return path;
	else
		return path.substr(pos+ 1);
}
//���ڵ���׷�ݵ�trace log
inline static void __trace_debug(const char* function,
								 const char* filename,int line,char* format, ...)
{
#ifdef __DEBUG__
	//������ú�������Ϣ
	fprintf(stdout,"��%s:%d��%s",GetFileName(filename).c_str(),line,function);

	//����û����trace��Ϣ
	va_list args;
	va_start(args,format);
	vfprintf(stdout,format,args);
	va_end(args);
#endif
}

#define __TRACE_DEBUG(...)  \
__trace_debug(__FUNCTION__,__FILE__,__LINE__,__VA_ARGS__);


typedef void(*FUNC_HANDLER)();

// һ���ռ�������
template <int inst>
class __MallocAllocTemplate
{
private:
	static void* OomMalloc(size_t n)
	{
		void* result;

		for (;;) {
			if (0 == __MallocAllocOomHandler)
			{
				throw bad_alloc("�����ڴ�ʧ��");
			}

			(*__MallocAllocOomHandler)();

			result = malloc(n);
			if (result)
				return(result);
		}
	}

	//static void (* __MallocAllocOomHandler)();
	static FUNC_HANDLER __MallocAllocOomHandler;

public:
	static void* Allocate(size_t n)
	{
		void *result = malloc(n);
		if (0 == result)
			result = OomMalloc(n);

		return result;
	}

	static void Deallocate(void *p, size_t /* n */)
	{
		free(p);
	}

	//static void (* SetMallocHandler(void (*f)()))()
	static FUNC_HANDLER SetMallocHandler(FUNC_HANDLER f)
	{
		FUNC_HANDLER old = __MallocAllocOomHandler;
		__MallocAllocOomHandler = f;
		return(old);
	}
};

template<int inst>
FUNC_HANDLER __MallocAllocTemplate<inst>::__MallocAllocOomHandler = 0;

typedef __MallocAllocTemplate<0> MallocAlloc;

# ifdef __USE_MALLOC

typedef MallocAlloc Alloc;

# else

// �����ռ�������

template <bool threads, int inst>
class __DefaultAllocTemplate {

	enum {__ALIGN = 8};			// ��׼ֵ
	enum {__MAX_BYTES = 128};	// ���ֵ
	enum {__NFREELISTS = __MAX_BYTES/__ALIGN};	// ��������Ĵ�С

	union Obj
	{
		union Obj* _freeListLink;	// ָ����һ���ڴ������ָ��
		char _clientData[1];		/* The client sees this.*/
	};

public:
	// 7 8 9
	static size_t FREELIST_INDEX(size_t bytes)
	{
		return (((bytes) + __ALIGN-1)/__ALIGN - 1);
	}

	// 7 8 9
	static size_t ROUND_UP(size_t bytes)
	{
		return (((bytes) + __ALIGN-1) & ~(__ALIGN - 1));
	}

	static char* ChunkAlloc(size_t n, size_t& nobjs)
	{
		char * result;
		size_t totalBytes = n * nobjs;
		size_t bytesLeft = _endFree - _startFree;

		// 
		// 1.�ڴ�����㹻20�������С�ռ䣬��ֱ�ӷ���
		// 2.����20��������1һ����ֻ�ܷ�����ٷ������
		// 3.����1��������ʣ��ռ䣬��ϵͳ���䡣
		//
		if (bytesLeft >= totalBytes)
		{
			result = _startFree;
			_startFree += totalBytes;
			return result;
		}
		else if (bytesLeft >= n)
		{
			nobjs = bytesLeft/n;
			totalBytes = nobjs*n;
			result = _startFree;
			_startFree += totalBytes;

			return result;
		}
		else
		{
			size_t bytesToGet = 2 * totalBytes + ROUND_UP(_heapSize >> 4);

			__TRACE_DEBUG("�ڴ��û�пռ䣬��ϵͳ����%dBytes\n",bytesToGet);

			// ��ʣ��ռ�ҵ���������
			if (bytesLeft > 0)
			{
				size_t index = FREELIST_INDEX(bytesLeft);
				((Obj*)_startFree)->_freeListLink = _freeList[index];
				_freeList[index] = (Obj*)_startFree;
			}

			_startFree = (char*)malloc(bytesToGet);
			if (_startFree == 0)
			{
				// ���������������λ��ȥȡ
				for (size_t i = n; i <= __MAX_BYTES; i+=__ALIGN)
				{
					size_t index = FREELIST_INDEX(i);
					if (_freeList[index])
					{
						Obj* first = _freeList[index];
						_startFree = (char*)first;
						_freeList[index] = first->_freeListLink;
						_endFree = _startFree+i;
						return ChunkAlloc(n, nobjs);
					}
				}

				// ���һ����������
				_startFree = (char*)__MallocAllocTemplate<inst>::Allocate(bytesToGet);

			}

			_endFree = _startFree+bytesToGet;
			_heapSize += bytesToGet;
			return ChunkAlloc(n, nobjs);
		}
	}

	static void* Refill(size_t n)
	{
		size_t nobjs = 20;
		char* chunk = ChunkAlloc(n, nobjs);

		__TRACE_DEBUG("���ڴ�ط������n:%d��nobjs:%d\n", n, nobjs);

		if (1 == nobjs)
			return chunk;

		size_t index = FREELIST_INDEX(n);

		// ��ʣ��Ŀ�ҵ���������
		Obj* cur = (Obj*)(chunk+n);
		_freeList[index] = cur;

		for (size_t i = 2; i < nobjs; ++i)
		{
			Obj* next = (Obj*)(chunk+n*i);
			cur->_freeListLink = next;
			cur = next;
		}

		cur->_freeListLink = NULL;
		return chunk;
	}

	static void* Allocate(size_t n)
	{
		__TRACE_DEBUG("�����ڴ�����%d\n", n);

		if (n > 128)
		{
			return MallocAlloc::Allocate(n);
		}

		// ������Ҫ���ڴ�������������е�λ��
		void* ret = 0;
		size_t index = FREELIST_INDEX(n);
		if (_freeList[index] == 0)
		{
			// ���ڴ�ؽ��з������
			ret = Refill(ROUND_UP(n));
		}
		else
		{
			// ͷɾ������ͷ�ڴ��
			ret = _freeList[index];
			_freeList[index] = ((Obj*)ret)->_freeListLink;
		}

		return ret;
	}
	
	static void Deallocate(void* p, size_t n)
	{
		__TRACE_DEBUG("�ͷ��ڴ�����%p, %d\n", p, n);

		if (n > 128)
		{
			MallocAlloc::Deallocate(p, n);
			return;
		}

		size_t index = FREELIST_INDEX(n);
		((Obj*)p)->_freeListLink = _freeList[index];
		_freeList[index] = (Obj*)p;
	}

private:
	// �������� 
	static Obj* _freeList[__NFREELISTS];

	// �ڴ��
	static char* _startFree; // �ڴ�ص���ʼλ��
	static char* _endFree;	 // �ڴ�ص�ˮλ��

	static size_t _heapSize; // �ѷ���ռ��С���������ڣ�
};

template <bool threads, int inst>
char* __DefaultAllocTemplate<threads, inst>::_startFree = 0;

template <bool threads, int inst>
char* __DefaultAllocTemplate<threads, inst>::_endFree = 0;

template <bool threads, int inst>
size_t __DefaultAllocTemplate<threads, inst>::_heapSize = 0;

template <bool threads, int inst>
typename __DefaultAllocTemplate<threads, inst>::Obj* __DefaultAllocTemplate<threads, inst>::_freeList[__NFREELISTS] = {0};

typedef __DefaultAllocTemplate<1, 0> Alloc;

#endif //__USE_MALLOC

template<class T, class Alloc>
class SimpleAlloc
{

public:
	static T* Allocate(size_t n)
	{ 
		return 0 == n? 0 : (T*) Alloc::Allocate(n * sizeof (T));	}

	static T* Allocate(void)
	{ 
		return (T*) Alloc::Allocate(sizeof (T)); 
	}

	static void Deallocate(T *p, size_t n)
	{ 
		if (0 != n)
			Alloc::Deallocate(p, n * sizeof (T));
	}

	static void Deallocate(T *p)
	{ 
		Alloc::Deallocate(p, sizeof (T));
	}
};

//�����ڴ�ص�һ������������������
void Test1()
{
	//���Ե���һ�������������ڴ�
	cout<<"���Ե���һ�������������ڴ�"<<endl;
	char*p1 = SimpleAlloc<char,Alloc>::Allocate(129);
	SimpleAlloc<char,Alloc>::Deallocate(p1, 129);

	//���Ե��ö��������������ڴ�
	cout<<"���Ե��ö��������������ڴ�"<<endl;
	char*p2=SimpleAlloc<char,Alloc>::Allocate(128);
	char*p3=SimpleAlloc<char,Alloc>::Allocate(128);
	char*p4=SimpleAlloc<char,Alloc>::Allocate(128);
	char*p5=SimpleAlloc<char,Alloc>::Allocate(128);
	SimpleAlloc<char,Alloc>::Deallocate(p2, 128);
	SimpleAlloc<char,Alloc>::Deallocate(p3, 128);
	SimpleAlloc<char,Alloc>::Deallocate(p4, 128);
	SimpleAlloc<char,Alloc>::Deallocate(p5, 128);

	for(int i= 0; i< 21; ++i)
	{
		printf("���Ե�%d�η���\n",i+1);
		char*p=SimpleAlloc<char,Alloc>::Allocate(128);
	}
}

// �׺в���
//�������ⳡ��
void Test2()
{
	cout<<"�����ڴ�ؿռ䲻������"<<endl;
	// 8*20->8*2->320
	char*p1=SimpleAlloc<char,Alloc>::Allocate(8);
	char*p2=SimpleAlloc<char,Alloc>::Allocate(8);

	// 16*20
	cout<<"�����ڴ�ؿռ䲻�㣬ϵͳ�ѽ��з���"<<endl;
	char*p3=SimpleAlloc<char,Alloc>::Allocate(12);
}

//����ϵͳ���ڴ�ľ��ĳ���
void Test3()
{
	cout<<"����ϵͳ���ڴ�ľ�"<<endl;

	SimpleAlloc<char,Alloc>::Allocate(1024*1024*1024);
	//SimpleAlloc<char, Alloc>::Allocate(1024*1024*1024);
	SimpleAlloc<char,Alloc>::Allocate(1024*1024);

	//���ò��ԣ�˵��ϵͳ����С���ڴ���������Ǻ�ǿ�ġ�
	for(int i= 0;i< 100000; ++i)
	{
		char*p1=SimpleAlloc<char,Alloc>::Allocate(128);
	}
}

