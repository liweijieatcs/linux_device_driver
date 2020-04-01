# =========================================
# Dynamic DMA mapping Guide 动态DMA映射指导
# =========================================
:Author: David S. Miller <davem@redhat.com><br>
:Author: Richard Henderson <rth@cygnus.com><br>
:Author: Jakub Jelinek <jakub@redhat.com><br>
<br>This is a guide to device driver writers on how to use the DMA API with example pseudo-code.  For a concise description of the API, see DMA-API.txt.<br>
<br>这是设备驱动程序编写者使用DMA API的指南 带有示例伪代码。有关API的简要说明，请参见 DMA-API.txt。<br>

## CPU and DMA addresses
## =====================
There are several kinds of addresses involved in the DMA API, and it'simportant to understand the differences.<br>
DMA API中涉及多种地址， 了解差异很重要。<br>

<br>The kernel normally uses virtual addresses.  Any address returned by kmalloc(), vmalloc(), and similar interfaces is a virtual address and can be stored in a ``void *``.<br>
<br>内核通常使用虚拟地址。通过kmalloc（），vmalloc（）和类似接口返回的地址是虚拟地址，可以被存储在"void *'"中。<br>

<br>The virtual memory system (TLB, page tables, etc.) translates virtual addresses to CPU physical addresses, which are stored as "phys_addr_t" or "resource_size_t".  The kernel manages device resources like registers as physical addresses.  These are the addresses in /proc/iomem.  The physical address is not directly useful to a driver; it must use ioremap() to map the space and produce a virtual address.<br>

<br>虚拟内存系统（TLB，页表等）将虚拟地址转换为CPU物理地址，并以"phys_addr_t"或"resource_size_t"存储。 内核通过物理地址管理设备资源，例如寄存器。 这些是/proc/iomem中的地址。 物理地址对驱动程序不是直接有用的。 它必须使用ioremap（）映射空间并生成一个虚拟地址。<br>

<br>I/O devices use a third kind of address: a "bus address".  If a device has registers at an MMIO address, or if it performs DMA to read or write system memory, the addresses used by the device are bus addresses.  In some systems, bus addresses are identical to CPU physical addresses, but in general they are not.  IOMMUs and host bridges can produce arbitrary mappings between physical and bus addresses.<br>
<br>I/O设备使用第三种地址：“总线地址”。 如果设备在MMIO地址上有寄存器，或者执行DMA来读取或写入系统内存，则设备使用的地址就是总线地址。 在某些系统中，总线地址与CPU物理地址相同，但通常不相同。 IOMMU和主机桥可以在物理地址和总线地址之间产生任意映射。<br>

<br>From a device's point of view, DMA uses the bus address space, but it may be restricted to a subset of that space.  For example, even if a system supports 64-bit addresses for main memory and PCI BARs, it may use an IOMMU so devices only need to use 32-bit DMA addresses.<br>

<br>从设备的角度来看，DMA使用总线地址空间，但可能仅限于该空间的子集。 例如，即使系统支持主存储器和PCI BAR的64位地址，它也可以使用IOMMU，因此设备仅需要使用32位DMA地址。<br>

<br>Here's a picture and some examples:<br>

                CPU                  CPU                  Bus
                Virtual              Physical             Address
                Address              Address               Space
                Space                Space

                +-------+             +------+             +------+
                |       |             |MMIO  |   Offset    |      |
                |       |  Virtual    |Space |   applied   |      |
              C +-------+ --------> B +------+ ----------> +------+ A
                |       |  mapping    |      |   by host   |      |
      +-----+   |       |             |      |   bridge    |      |   +--------+
      |     |   |       |             +------+             |      |   |        |
      | CPU |   |       |             | RAM  |             |      |   | Device |
      |     |   |       |             |      |             |      |   |        |
      +-----+   +-------+             +------+             +------+   +--------+
                |       |  Virtual    |Buffer|   Mapping   |      |
              X +-------+ --------> Y +------+ <---------- +------+ Z
                |       |  mapping    | RAM  |   by IOMMU
                |       |             |      |
                |       |             |      |
                +-------+             +------+

<br>During the enumeration process, the kernel learns about I/O devices and their MMIO space and the host bridges that connect them to the system.  For example, if a PCI device has a BAR, the kernel reads the bus address (A) from the BAR and converts it to a CPU physical address (B).  The address B is stored in a struct resource and usually exposed via /proc/iomem.  When a driver claims a device, it typically uses ioremap() to map physical address B at a virtual address (C).  It can then use, e.g., ioread32(C), to access the device registers at bus address A.<br>

<br>在自举过程中，内核了解I/O设备及其MMIO空间以及将它们连接到系统的主机桥。例如，如果PCI设备具有BAR，则内核从BAR读取总线地址(A)，并将其转换为CPU物理地址(B)。地址B存储在结构资源中，通常通过/proc/iomem导出。 当驱动请求设备时，通常使用ioremap()将物理地址B映射到虚拟地址(C)。 然后可以使用例如ioread32(C)访问总线地址A处的设备寄存器。<br>

<br>If the device supports DMA, the driver sets up a buffer using kmalloc() or a similar interface, which returns a virtual address (X).  The virtual memory system maps X to a physical address (Y) in system RAM.  The driver can use virtual address X to access the buffer, but the device itself cannot because DMA doesn't go through the CPU virtual memory system.<br>

<br>如果设备支持DMA，则驱动程序将使用kmalloc（）或类似接口设置缓冲区，该缓冲区将返回虚拟地址（X）。 虚拟内存系统将X映射到系统RAM中的物理地址（Y）。 驱动程序可以使用虚拟地址X来访问缓冲区，但是设备本身不能，因为DMA不会绕过CPU虚拟内存系统。<br>

<br>In some simple systems, the device can do DMA directly to physical address Y.  But in many others, there is IOMMU hardware that translates DMA addresses to physical addresses, e.g., it translates Z to Y.  This is part of the reason for the DMA API: the driver can give a virtual address X to an interface like dma_map_single(), which sets up any required IOMMU mapping and returns the DMA address Z.  The driver then tells the device to do DMA to Z, and the IOMMU maps it to the buffer at address Y in system RAM.<br>

<br>在某些简单的系统中，设备可以直接对物理地址Y进行DMA。但是在许多其他系统中，有IOMMU硬件将DMA地址转换为物理地址，例如，将Z转换为Y。这是使用DMA API的部分原因：驱动程序可以将虚拟地址X提供给dma_map_single（）之类的接口，该接口将设置任何必需的IOMMU映射并返回DMA地址Z。驱动程序告知设备对Z进行DMA，然后IOMMU将其映射到系统RAM地址Y处的缓冲区。<br>

<br>So that Linux can use the dynamic DMA mapping, it needs some help from the drivers, namely it has to take into account that DMA addresses should be mapped only for the time they are actually used and unmapped after the DMA transfer.<br>

<br>为了使Linux能够使用动态DMA映射，它需要驱动程序提供一些帮助，即必须考虑到DMA地址,仅在真正使用的时候被映射，在传输完之后解除映射。<br>

<br>The following API will work of course even on platforms where no such hardware exists.<br>
<br>即使在不存在此类硬件的平台上，以下API当然也可以工作<br>

<br>Note that the DMA API works with any bus independent of the underlying microprocessor architecture. You should use the DMA API rather than the bus-specific DMA API, i.e., use the dma_map_*() interfaces rather than the pci_map_*() interfaces.<br>
<br>请注意，DMA API可与任何独立于底层微处理器体系结构的总线一起使用。 您应该使用DMA API而不是特定于总线的DMA API，即使用dma_map _ *（）接口而不是pci_map _ *（）接口。<br>

<br>First of all, you should make sure:
```c
	#include <linux/dma-mapping.h>
```
is in your driver, which provides the definition of dma_addr_t.  This type can hold any valid DMA address for the platform and should be used everywhere you hold a DMA address returned from the DMA mapping functions.<br>
<br>首先，应该包含头文件<liux/dma-mapping.h>在你的驱动程序中，该驱动程序提供了dma_addr_t的定义。 此类型可以容纳该平台的任何有效DMA地址，并且应在拥有从DMA映射函数返回的DMA地址的任何地方使用。<br>

## What memory is DMA'able? DMA可以使用什么内存？
## =====================

<br>The first piece of information you must know is what kernel memory can be used with the DMA mapping facilities.  There has been an unwritten set of rules regarding this, and this text is an attempt to finally write them down.<br>
<br>您必须知道的第一条信息是DMA映射工具可以使用哪些内核内存。 与此相关的规则有一套未成文的规则，本文旨在最终将它们写下来。<br>

<br>If you acquired your memory via the page allocator (i.e. __get_free_page*()) or the generic memory allocators (i.e. kmalloc() or kmem_cache_alloc()) then you may DMA to/from that memory using the addresses returned from those routines.<br>
<br>如果您是通过页面分配器（即__get_free_page *（））或通用内存分配器（即kmalloc（）或kmem_cache_alloc（））获取内存的，则可以使用从这些例程返回的地址向该内存进行DMA访问。<br>

<br>This means specifically that you may _not_ use the memory/addresses returned from vmalloc() for DMA.  It is possible to DMA to the _underlying_ memory mapped into a vmalloc() area, but this requires walking page tables to get the physical addresses, and then translating each of those pages back to a kernel address using something like __va().  [ EDIT: Update this when we integrate Gerd Knorr's generic code which does this. ]<br>
<br>特别是这意味着您可能无法将vmalloc（）返回的内存/地址用于DMA。 可以将DMA映射到映射到vmalloc（）区域中的内存，但这需要遍历页表以获取物理地址，然后使用诸如va（）之类的东西将这些页面中的每一个转换回内核地址。 [编辑：当我们集成Gerd Knorr的通用代码来执行此操作时，请对此进行更新。 ]<br>

<br>This rule also means that you may use neither kernel image addresses (items in data/text/bss segments), nor module image addresses, nor stack addresses for DMA.  These could all be mapped somewhere entirely different than the rest of physical memory.  Even if those classes of memory could physically work with DMA, you'd need to ensure the I/O buffers were cacheline-aligned.  Without that, you'd see cacheline sharing problems (data corruption) on CPUs with DMA-incoherent caches. (The CPU could write to one word, DMA would write to a different one in the same cache line, and one of them could be overwritten.)<br>
<br>此规则还意味着您既不能使用内核映像地址（数据/文本/ bss段中的项），也不能使用模块映像地址，也不能使用DMA的堆栈地址。 这些都可以映射到与其余物理内存完全不同的地方。 即使这些内存类别在物理上可以与DMA配合使用，您也需要确保I/O缓冲区与缓存行对齐。 否则，您将在具有DMA不相关缓存的CPU上看到缓存行共享问题（数据损坏）。 （CPU可以写入一个字，DMA可以写入同一高速缓存行中的另一个字，并且其中一个可以被覆盖。）<br>

<br>Also, this means that you cannot take the return of a kmap() call and DMA to/from that.  This is similar to vmalloc().<br>
<br>同样，这意味着您无法对kmap()的地址进行DMA映射。 这类似于vmalloc（）。<br>

<br>What about block I/O and networking buffers?  The block I/O and networking subsystems make sure that the buffers they use are valid for you to DMA from/to.<br>
<br>块I / O和网络缓冲区呢？ 块I / O和网络子系统确保它们从DMA读取/写入使用的缓冲区有效。<br>
