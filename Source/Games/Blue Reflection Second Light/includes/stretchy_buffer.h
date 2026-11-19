#pragma once

class StretchyBuffer
{
public:
   StretchyBuffer(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t initial_capacity)
       : capacity(0),
         size(0)
   {
      this->device = device;
      Resize(context, initial_capacity);
   }

   void CopyFromBuffer(ID3D11DeviceContext* context, ID3D11Buffer* srcBuffer, uint32_t byteCount)
   {
      while (size + byteCount > capacity)
      {
         Resize(context, capacity * 2);
      }

      {
          D3D11_BOX srcBox = {};
          srcBox.left = 0;
          srcBox.top = 0;
          srcBox.front = 0;
          srcBox.right = byteCount;
          srcBox.bottom = 1;
          srcBox.back = 1;
          context->CopySubresourceRegion(buffer.get(), 0, size, 0, 0, srcBuffer, 0, &srcBox);
      }

      size += byteCount;
   }

   void Reset()
   {
      size = 0;
   }

   void Resize(ID3D11DeviceContext* context, uint32_t new_capacity)
   {
      ComPtr<ID3D11Buffer> old_buffer = buffer;

      {
         D3D11_BUFFER_DESC bd = {};
         bd.ByteWidth = new_capacity;
         bd.Usage = D3D11_USAGE_DEFAULT;
         bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         bd.CPUAccessFlags = 0;
         bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
         bd.StructureByteStride = 0;
         device->CreateBuffer(&bd, nullptr, buffer.put());
      }
      {
         D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
         srvd.Format = DXGI_FORMAT_R32_TYPELESS;
         srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
         srvd.Buffer.FirstElement = 0;
         srvd.Buffer.NumElements = new_capacity / 4;
         srvd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
         device->CreateShaderResourceView(buffer.get(), &srvd, srv.put());
      }
      capacity = new_capacity;

      if (old_buffer)
      {
         D3D11_BOX srcBox = {};
         srcBox.left = 0;
         srcBox.top = 0;
         srcBox.front = 0;
         srcBox.right = size;
         srcBox.bottom = 1;
         srcBox.back = 1;
         context->CopySubresourceRegion(buffer.get(), 0, 0, 0, 0, old_buffer.get(), 0, &srcBox);
      }
   }

   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11Buffer> buffer;
   ComPtr<ID3D11ShaderResourceView> srv;
   uint32_t capacity;
   uint32_t size;
};

class StretchyCpuBuffer
{
public:
   StretchyCpuBuffer()
    : capacity(0),
      size(0)
   {
   }
   
   StretchyCpuBuffer(uint32_t initial_capacity)
   {
      buffer.resize(initial_capacity);
      capacity = initial_capacity;
      size = 0;
   }

   void CopyFromMemory(const void* srcData, uint32_t byteCount, uint32_t byteCount256Align)
   {
      while (size + byteCount256Align > capacity)
      {
         Resize(capacity * 2);
      }

      std::memcpy(buffer.data() + size, srcData, byteCount);
      size += byteCount256Align;
   }

   void Reset()
   {
      size = 0;
   }

   void Resize(uint32_t new_capacity)
   {
      buffer.resize(new_capacity);
      capacity = new_capacity;
   }

   uint8_t* Data()
   {
      return buffer.data();
   }

public:
   std::vector<uint8_t> buffer;
   uint32_t capacity = 0;
   uint32_t size = 0;
};

class StretchyGpuBuffer
{
public:
   void Init(ID3D11Device* device, uint32_t initial_capacity)
   {
      this->device = device;
      capacity = initial_capacity;

      D3D11_BUFFER_DESC bd = {};
      bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      bd.ByteWidth = capacity;
      bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      bd.MiscFlags = 0;
      bd.StructureByteStride = 0;
      bd.Usage = D3D11_USAGE_DYNAMIC;

      device->CreateBuffer(&bd, nullptr, buffer.put());
   }
   
   void Resize(ID3D11DeviceContext* context, uint32_t new_capacity)
   {
      ComPtr<ID3D11Buffer> old_buffer = buffer;

      D3D11_BUFFER_DESC bd = {};
      bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      bd.ByteWidth = new_capacity;
      bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      bd.MiscFlags = 0;
      bd.StructureByteStride = 0;
      bd.Usage = D3D11_USAGE_DYNAMIC;

      device->CreateBuffer(&bd, nullptr, buffer.put());
      capacity = new_capacity;

      if (old_buffer)
      {
         D3D11_BOX srcBox = {};
         srcBox.left = 0;
         srcBox.top = 0;
         srcBox.front = 0;
         srcBox.right = size;
         srcBox.bottom = 1;
         srcBox.back = 1;
         context->CopySubresourceRegion(buffer.get(), 0, 0, 0, 0, old_buffer.get(), 0, &srcBox);
      }
   }

   void Upload(ID3D11DeviceContext* context, const void* data, uint32_t size)
   {
      if (size > capacity)
      {
         Resize(context, size);
      }
      
      D3D11_MAPPED_SUBRESOURCE mapped;
      context->Map(buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      memcpy(mapped.pData, data, size);
      context->Unmap(buffer.get(), 0);
      
      this->size = size;
   }
   
   void Reset()
   {
      size = 0;
   }
   
public:
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11Buffer> buffer;
   uint32_t capacity;
   uint32_t size;
};