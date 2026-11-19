#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>

// Reference counted COM pointer.
// - ComPtr(T*) and operator=(T*) take a new reference (AddRef), attach(T*) takes ownership of one you already own.
// - operator& is const: it is only usable where a `T* const*` is expected (a `*Set*` call). Out-parameters (`Create*`, `Get*`,
//   `QueryInterface`) don't compile with it, use put() (releases the old object first, so it can't leak).
// - A ComPtr<Derived> converts to ComPtr<Base> (copy or move), replacing casts through ComPtr<>&&.
// - Arrays of ComPtr can be passed as arrays of raw pointers, see PutArray() and AddressOfArray() below.
template<typename T>
class ComPtr
{
	template<typename U>
	friend class ComPtr;

	// A ComPtr<U> can only be used as a ComPtr<T> if U* is a T*
	template<typename U>
	using EnableIfConvertible = std::enable_if_t<std::is_convertible_v<U*, T*> && !std::is_same_v<U, T>, int>;

public:

	ComPtr() noexcept
		: ptr(nullptr)
	{}

	ComPtr(T* const other) noexcept
		: ptr(other)
	{
		if (ptr)
		{
			ptr->AddRef();
		}
	}

	ComPtr(const ComPtr<T>& other) noexcept
		: ptr(other.ptr)
	{
		if (ptr)
		{
			ptr->AddRef();
		}
	}

	ComPtr(ComPtr<T>&& other) noexcept
		: ptr(other.ptr)
	{
		other.ptr = nullptr;
	}

	template<typename U, EnableIfConvertible<U> = 0>
	ComPtr(const ComPtr<U>& other) noexcept
		: ptr(other.ptr)
	{
		if (ptr)
		{
			ptr->AddRef();
		}
	}

	template<typename U, EnableIfConvertible<U> = 0>
	ComPtr(ComPtr<U>&& other) noexcept
		: ptr(other.ptr)
	{
		other.ptr = nullptr;
	}

	~ComPtr() noexcept
	{
		if (ptr)
		{
			ptr->Release();
		}
	}

public:

	ComPtr<T>& operator=(T* const other) noexcept
	{
		if (ptr != other)
		{
			if (other)
			{
				other->AddRef();
			}
			
			if (ptr)
			{
				ptr->Release();
			}
			
			ptr = other;
		}

		return *this;
	}

	ComPtr<T>& operator=(const ComPtr<T>& other) noexcept
	{
		if (ptr != other.ptr)
		{
			if (other.ptr)
			{
				other.ptr->AddRef();
			}
			
			if (ptr)
			{
				ptr->Release();
			}
			
			ptr = other.ptr;
		}

		return *this;
	}

	ComPtr<T>& operator=(ComPtr<T>&& other) noexcept
	{
		if (ptr != other.ptr)
		{
			if (ptr)
			{
				ptr->Release();
			}
			
			ptr = other.ptr;
			other.ptr = nullptr;
		}

		return *this;
	}

	template<typename U, EnableIfConvertible<U> = 0>
	ComPtr<T>& operator=(const ComPtr<U>& other) noexcept
	{
		return operator=(static_cast<T*>(other.ptr));
	}

	template<typename U, EnableIfConvertible<U> = 0>
	ComPtr<T>& operator=(ComPtr<U>&& other) noexcept
	{
		// Different types can't be the same object holder, but they can hold the same pointer
		T* const new_ptr = other.ptr;
		other.ptr = nullptr;
		if (ptr != new_ptr)
		{
			if (ptr)
			{
				ptr->Release();
			}
			ptr = new_ptr;
		}
		else if (new_ptr)
		{
			// We were already holding it, drop the reference the moved-from pointer owned
			new_ptr->Release();
		}

		return *this;
	}

public:

	T* operator->() const noexcept
	{
		return ptr;
	}

	T& operator*() const noexcept
	{
		return *ptr;
	}

	T*const* operator&() const noexcept
	{
		return &ptr;
	}

	explicit operator bool() const noexcept
	{
		return ptr != nullptr;
	}

public:

	T* get() const noexcept
	{
		return ptr;
	}

	T*const* get_addressof() const noexcept
	{
		return &ptr;
	}

	// The current COM reference count of the object (0 if empty), for diagnostics
	unsigned long ref_count() const noexcept
	{
		if (!ptr)
		{
			return 0;
		}
		ptr->AddRef();
		return ptr->Release();
	}

	T** put() noexcept
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}

		return &ptr;
	}

	void attach(T* const other) noexcept
	{
		if (ptr)
		{
			ptr->Release();
		}

		ptr = other;
	}

	T* detach() noexcept
	{
		T* tmp = ptr;
		ptr = nullptr;
		return tmp;
	}

	void reset() noexcept
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}

private:

	T* ptr;
};

template<typename T, typename U>
bool operator==(const ComPtr<T>& left, const ComPtr<U>& right) noexcept
{
	return left.get() == right.get();
}

template<typename T, typename U>
bool operator==(const ComPtr<T>& left, const U* const right) noexcept
{
	return left.get() == right;
}

template<typename T, typename U>
bool operator==(const T* const left, const ComPtr<U>& right) noexcept
{
	return left == right.get();
}

template<typename T>
bool operator==(const ComPtr<T>& left, std::nullptr_t) noexcept
{
	return left.get() == nullptr;
}

template<typename T>
bool operator==(std::nullptr_t, const ComPtr<T>& right) noexcept
{
	return nullptr == right.get();
}

template<typename T, typename U>
bool operator!=(const ComPtr<T>& left, const ComPtr<U>& right) noexcept
{
	return left.get() != right.get();
}

template<typename T, typename U>
bool operator!=(const ComPtr<T>& left, const U* const right) noexcept
{
	return left.get() != right;
}

template<typename T, typename U>
bool operator!=(const T* const left, const ComPtr<U>& right) noexcept
{
	return left != right.get();
}

template<typename T>
bool operator!=(const ComPtr<T>& left, std::nullptr_t) noexcept
{
	return left.get() != nullptr;
}

template<typename T>
bool operator!=(std::nullptr_t, const ComPtr<T>& right) noexcept
{
	return nullptr != right.get();
}

template<typename T, typename U>
bool operator<(const ComPtr<T>& left, const ComPtr<U>& right) noexcept
{
	return left.get() < right.get();
}

// D3D11 `*Get*` calls write N pointers (OMGetRenderTargets, PSGetShaderResources...) and the `*Set*` calls read N, so arrays of ComPtr are
// used as arrays of raw pointers. This relies on ComPtr<T> being exactly a T* (asserted below).
// For out-parameters: releases every element, then returns the array as `T**`. Pass it where `&array[0]` was passed.
template<typename T, size_t N>
T** PutArray(ComPtr<T> (&array)[N]) noexcept
{
	static_assert(sizeof(ComPtr<T>) == sizeof(T*));
	for (ComPtr<T>& element : array)
	{
		element.reset();
	}
	return reinterpret_cast<T**>(std::addressof(array[0]));
}

// For in-parameters: the array as `T* const*`, nothing is released or AddRef-ed
template<typename T, size_t N>
T* const* AddressOfArray(const ComPtr<T> (&array)[N]) noexcept
{
	static_assert(sizeof(ComPtr<T>) == sizeof(T*));
	return reinterpret_cast<T* const*>(std::addressof(array[0]));
}
