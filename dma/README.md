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

## DMA addressing capabilities DMA的寻址能力？
## =====================
<br>By default, the kernel assumes that your device can address 32-bits of DMA addressing.  For a 64-bit capable device, this needs to be increased, and for a device with limitations, it needs to be decreased.<br>

<br>默认情况下，内核假定您的设备可以寻址DMA的32位 寻址。对于支持64位的设备，此功能需要增加，对于 有局限性的设备，需要减少。<br>

<br>Special note about PCI: PCI-X specification requires PCI-X devices to support 64-bit addressing (DAC) for all transactions.  And at least one platform (SGI SN2) requires 64-bit consistent allocations to operate correctly when the IO bus is in PCI-X mode.<br>
<br>关于PCI的特别说明：PCI-X规范要求PCI-X设备支持64位寻址（DAC）。 当IO总线处于PCI-X模式时，至少有一个平台（SGI SN2）需要64位一致的分配才能正确运行。<br>
<br>For correct operation, you must set the DMA mask to inform the kernel about your devices DMA addressing capabilities.
<br>
<br>为了正确操作，您必须设置DMA掩码以通知内核有关 您的设备的DMA寻址功能。<br>
<br>This is performed via a call to dma_set_mask_and_coherent():<br>
<br>这是通过调用dma_set_mask_and_coherent（）来执行的：<br>
```c
int dma_set_mask_and_coherent(struct device *dev, u64 mask);
```
<br>which will set the mask for both streaming and coherent APIs together.  If you have some special requirements, then the following two separate calls can be used instead:<br>
<br>它将同时为流式API和相关API设置掩码。如果你 有一些特殊要求，那么可以进行以下两个单独的调用 改为使用：<br>
<br>The setup for streaming mappings is performed via a call to dma_set_mask()::<br>
<br>通过dma_set_mask进行流式映射<br>
```c
int dma_set_mask(struct device *dev, u64 mask);
```
<br>The setup for consistent allocations is performed via a call to dma_set_coherent_mask():<br>
<br>通过dma_set_coherent_mask进行一致性映射<br>
```c
int dma_set_coherent_mask(struct device *dev, u64 mask);
```
<br>Here, dev is a pointer to the device struct of your device, and mask is a bit mask describing which bits of an address your device supports.  Often the device struct of your device is embedded in the bus-specific device struct of your device.  For example, &pdev->dev is a pointer to the device struct of a PCI device (pdev is a pointer to the PCI device struct of your device).<br>
<br>这里，dev是指向您设备的设备结构的指针，而mask是描述您设备支持的地址的哪些位的位掩码。 设备的设备结构通常嵌入在设备的总线特定设备结构中。 例如，＆pdev-> dev是指向PCI设备的设备结构的指针（pdev是指向设备的PCI设备结构的指针）。<br>

<br>These calls usually return zero to indicated your device can perform DMA properly on the machine given the address mask you provided, but they might return an error if the mask is too small to be supportable on the given system.  If it returns non-zero, your device cannot perform DMA properly on this platform, and attempting to do so will result in undefined behavior. You must not use DMA on this device unless the dma_set_mask family of functions has returned success.<br>
<br>这些调用通常返回零，以表明您的设备可以在给定您提供的地址掩码的情况下在机器上正确执行DMA，但是如果掩码太小而无法在给定的系统上支持，它们可能会返回错误。 如果返回非零值，则您的设备将无法在此平台上正确执行DMA，并且尝试执行DMA将导致不确定的行为。 除非dma_set_mask系列功能返回成功，否则不得在此设备上使用DMA。<br>
<br>This means that in the failure case, you have two options:<br>
<br>这意味着在失败的情况下，您有两个选择：<br>
1) Use some non-DMA mode for data transfer, if possible. 如果可能，请使用某些非DMA模式进行数据传输。
2) Ignore this device and do not initialize it. 忽略此设备，不要初始化它。
<br>It is recommended that your driver print a kernel KERN_WARNING message when setting the DMA mask fails.  In this manner, if a user of your driver reports that performance is bad or that the device is not even detected, you can ask them for the kernel messages to find out exactly why.<br>
<br>当设置DMA掩码失败时，建议驱动程序打印内核KERN_WARNING消息。 通过这种方式，如果驱动程序的用户报告性能差或什至没有检测到设备，则可以要求他们提供内核消息以找出原因。<br>
<br>The standard 64-bit addressing device would do something like this:<br>
<br>标准的64位寻址设备将执行以下操作：<br>
```c
	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64))) {
		dev_warn(dev, "mydev: No suitable DMA available\n");
		goto ignore_this_device;
	}
```
<br>If the device only supports 32-bit addressing for descriptors in the coherent allocations, but supports full 64-bits for streaming mappings it would look like this:<br>
<br>如果设备仅支持一致性分配中的描述符的32位寻址，但支持完整的64位流映射，则它将如下所示：<br>
```c
	if (dma_set_mask(dev, DMA_BIT_MASK(64))) {
		dev_warn(dev, "mydev: No suitable DMA available\n");
		goto ignore_this_device;
	}
```
<br>The coherent mask will always be able to set the same or a smaller mask as the streaming mask. However for the rare case that a device driver only uses consistent allocations, one would have to check the return value from dma_set_coherent_mask().
<br>
<br>相干一致性掩码始终和流式掩码一样，或者比流式掩码小。 但是，在极少数情况下，设备驱动程序仅使用一致的分配，则必须检查dma_set_coherent_mask（）的返回值。<br>
<br>Finally, if your device can only drive the low 24-bits of address you might do something like:<br>
<br>最后，如果您的设备只能驱动低24位地址，则可以执行以下操作：<br>
```c
	if (dma_set_mask(dev, DMA_BIT_MASK(24))) {
		dev_warn(dev, "mydev: 24-bit DMA addressing not available\n");
		goto ignore_this_device;
	}
```
<br>When dma_set_mask() or dma_set_mask_and_coherent() is successful, and returns zero, the kernel saves away this mask you have provided. The kernel will use this information later when you make DMA mappings.<br>
<br>当dma_set_mask（）或dma_set_mask_and_coherent（）成功并返回零时，内核将保存您提供的该掩码。 稍后进行DMA映射时，内核将使用此信息。<br>

<br>There is a case which we are aware of at this time, which is worth mentioning in this documentation.  If your device supports multiple functions (for example a sound card provides playback and record functions) and the various different functions have _different_ DMA addressing limitations, you may wish to probe each mask and only provide the functionality which the machine can handle.  It is important that the last call to dma_set_mask() be for the most specific mask.<br>
<br>目前，我们知道一个案例，在本文档中值得一提。 如果您的设备支持多种功能（例如，声卡提供播放和录音功能）并且各种不同的功能都具有不同的DMA寻址限制，则您可能希望探查每个掩码，而仅提供机器可以处理的功能。 重要的是，对dma_set_mask（）的最后一次调用应针对最特定的掩码<br>

<br>Here is pseudo-code showing how this might be done:<br>
<br>这是伪代码，显示了如何完成此操作：<br>
```c
	#define PLAYBACK_ADDRESS_BITS	DMA_BIT_MASK(32)
	#define RECORD_ADDRESS_BITS	DMA_BIT_MASK(24)

	struct my_sound_card *card;
	struct device *dev;

	...
	if (!dma_set_mask(dev, PLAYBACK_ADDRESS_BITS)) {
		card->playback_enabled = 1;
	} else {
		card->playback_enabled = 0;
		dev_warn(dev, "%s: Playback disabled due to DMA limitations\n",
		       card->name);
	}
	if (!dma_set_mask(dev, RECORD_ADDRESS_BITS)) {
		card->record_enabled = 1;
	} else {
		card->record_enabled = 0;
		dev_warn(dev, "%s: Record disabled due to DMA limitations\n",
		       card->name);
	}
```
<br>A sound card was used as an example here because this genre of PCI devices seems to be littered with ISA chips given a PCI front end, and thus retaining the 16MB DMA addressing limitations of ISA.<br>
<br>此处以声卡为例，因为在给定PCI前端的情况下，这种类型的PCI设备似乎散布着ISA芯片，因此保留了ISA的16MB DMA寻址限制。<br>

## Types of DMA mappings
## =====================
<br>There are two types of DMA mappings:<br>
<br>有两种类型的DMA映射<br>

<br>Consistent DMA mappings which are usually mapped at driver initialization, unmapped at the end and for which the hardware should guarantee that the device and the CPU can access the data in parallel and will see updates made by each other without any explicit software flushing.<br>
<br>一致的DMA映射通常在驱动程序初始化时进行映射，退出的时候取消映射，并且硬件应保证设备和CPU可以并行访问数据，并且可以看到彼此进行的更新，而无需任何显式的软件刷新。<br>

<br>Think of "consistent" as "synchronous" or "coherent".<br>
<br>将“一致”视为“同步”或“连贯”。<br>

<br>The current default is to return consistent memory in the low 32 bits of the DMA space.  However, for future compatibility you should set the consistent mask even if this default is fine for your driver.<br>
<br>当前的默认值是在DMA空间的低32位中返回一致的内存。 但是，为了将来的兼容性，即使此默认值适合您的驱动程序，也应设置一致的掩码。<br>

<br>Good examples of what to use consistent mappings for are:<br>
<br>关于使用一致映射的示例如下：<br>

- Network card DMA ring descriptors.网卡DMA环描述符。
- SCSI adapter mailbox command data structures.SCSI适配器邮箱命令数据结构。
- Device firmware microcode executed out of main memory.设备固件微码从主存储器中执行。

<br>The invariant these examples all require is that any CPU store to memory is immediately visible to the device, and vice versa.  Consistent mappings guarantee this.<br>
<br>这些示例都要求不变的是，任何存储在内存中的CPU都对设备立即可见，反之亦然。 一致的映射可确保这一点。<br>

  .. important:

<br>Consistent DMA memory does not preclude the usage of proper memory barriers.  The CPU may reorder stores to consistent memory just as it may normal memory. <br>
<br>一致性DMA内存并不排除使用适当的内存屏障。 CPU可能会将存储重新排序到一致的内存，就像它可能会正常存储一样。<br>
<br>Example: <br>
<br>if it is important for the device to see the first word of a descriptor updated before the second, you must do something like::<br>
<br>如果让设备看到描述符的第一个word0在第二个word1之前更新很重要，则必须执行以下操作：<br>
```c
  desc->word0 = address;
  wmb();
  desc->word1 = DESC_VALID;
```
<br>in order to get correct behavior on all platforms.<br>
<br>为了在所有平台上获得正确的行为。<br>

<br>Also, on some platforms your driver may need to flush CPU write buffers in much the same way as it needs to flush write buffers found in PCI bridges (such as by reading a register's value after writing it).<br>
<br>同样，在某些平台上，驱动程序可能需要刷新CPU写缓冲区的方式与刷新PCI桥中发现的写缓冲区的方式相同（例如，在写寄存器后读取寄存器的值）。<br>

<br>Streaming DMA mappings which are usually mapped for one DMA transfer, unmapped right after it (unless you use dma_sync_* below) and for which hardware can optimize for sequential accesses.<br>
<br>流式DMA通常映射为一个DMA传输，紧接其后是未映射的（除非您在下面使用dma_sync_ *），并且硬件可以针对顺序访问进行优化。<br>

<br>Think of "streaming" as "asynchronous" or "outside the coherency domain".<br>
<br>将“流”视为“异步”或“在一致性域之外”。<br>

Good examples of what to use streaming mappings for are:
<br>流映射用于什么的很好的例子是：<br>
- Networking buffers transmitted/received by a device. 设备发送/接收的网络缓冲区。
- Filesystem buffers written/read by a SCSI device.SCSI设备写入/读取的文件系统缓冲区。

<br>The interfaces for using this type of mapping were designed in such a way that an implementation can make whatever performance optimizations the hardware allows.  To this end, when using such mappings you must be explicit about what you want to happen.<br>
<br>设计使用这种类型的映射的接口，以便实现可以对硬件进行任何性能优化。 为此，在使用此类映射时，必须明确要发生的事情。<br>

<br>Neither type of DMA mapping has alignment restrictions that come from the underlying bus, although some devices may have such restrictions. Also, systems with caches that aren't DMA-coherent will work better when the underlying buffers don't share cache lines with other data.<br>
<br>两种类型的DMA映射都没有来自基础总线的对齐限制，尽管某些设备可能有这种限制。 同样，当基础缓冲区不与其他数据共享高速缓存行时，具有非DMA一致性高速缓存的系统会更好地工作。<br>

## Using Consistent DMA mappings 使用一致性的DMA映射
## ================================================