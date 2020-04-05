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
<br>内核通常使用虚拟地址。通过kmalloc()，vmalloc()和类似接口返回的地址是虚拟地址，可以被存储在"void *'"中。<br>

<br>The virtual memory system (TLB, page tables, etc.) translates virtual addresses to CPU physical addresses, which are stored as "phys_addr_t" or "resource_size_t".  The kernel manages device resources like registers as physical addresses.  These are the addresses in /proc/iomem.  The physical address is not directly useful to a driver; it must use ioremap() to map the space and produce a virtual address.<br>

<br>虚拟内存系统(TLB，页表等)将虚拟地址转换为CPU物理地址，并以"phys_addr_t"或"resource_size_t"存储。 内核通过物理地址管理设备资源，例如寄存器。 这些是/proc/iomem中的地址。 物理地址对驱动程序不是直接有用的。 它必须使用ioremap()映射空间并生成一个虚拟地址。<br>

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

<br>如果设备支持DMA，则驱动程序将使用kmalloc()或类似接口设置缓冲区，该缓冲区将返回虚拟地址(X)。 虚拟内存系统将X映射到系统RAM中的物理地址(Y)。 驱动程序可以使用虚拟地址X来访问缓冲区，但是设备本身不能，因为DMA不会绕过CPU虚拟内存系统。<br>

<br>In some simple systems, the device can do DMA directly to physical address Y.  But in many others, there is IOMMU hardware that translates DMA addresses to physical addresses, e.g., it translates Z to Y.  This is part of the reason for the DMA API: the driver can give a virtual address X to an interface like dma_map_single(), which sets up any required IOMMU mapping and returns the DMA address Z.  The driver then tells the device to do DMA to Z, and the IOMMU maps it to the buffer at address Y in system RAM.<br>

<br>在某些简单的系统中，设备可以直接对物理地址Y进行DMA。但是在许多其他系统中，有IOMMU硬件将DMA地址转换为物理地址，例如，将Z转换为Y。这是使用DMA API的部分原因：驱动程序可以将虚拟地址X提供给dma_map_single()之类的接口，该接口将设置任何必需的IOMMU映射并返回DMA地址Z。驱动程序告知设备对Z进行DMA，然后IOMMU将其映射到系统RAM地址Y处的缓冲区。<br>

<br>So that Linux can use the dynamic DMA mapping, it needs some help from the drivers, namely it has to take into account that DMA addresses should be mapped only for the time they are actually used and unmapped after the DMA transfer.<br>

<br>为了使Linux能够使用动态DMA映射，它需要驱动程序提供一些帮助，即必须考虑到DMA地址,仅在真正使用的时候被映射，在传输完之后解除映射。<br>

<br>The following API will work of course even on platforms where no such hardware exists.<br>
<br>即使在不存在此类硬件的平台上，以下API当然也可以工作<br>

<br>Note that the DMA API works with any bus independent of the underlying microprocessor architecture. You should use the DMA API rather than the bus-specific DMA API, i.e., use the dma_map_*() interfaces rather than the pci_map_*() interfaces.<br>
<br>请注意，DMA API可与任何独立于底层微处理器体系结构的总线一起使用。 您应该使用DMA API而不是特定于总线的DMA API，即使用dma_map _ *()接口而不是pci_map _ *()接口。<br>

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
<br>如果您是通过页面分配器(即__get_free_page *())或通用内存分配器(即kmalloc()或kmem_cache_alloc())获取内存的，则可以使用从这些例程返回的地址向该内存进行DMA访问。<br>

<br>This means specifically that you may _not_ use the memory/addresses returned from vmalloc() for DMA.  It is possible to DMA to the _underlying_ memory mapped into a vmalloc() area, but this requires walking page tables to get the physical addresses, and then translating each of those pages back to a kernel address using something like __va().  [ EDIT: Update this when we integrate Gerd Knorr's generic code which does this. ]<br>
<br>特别是这意味着您可能无法将vmalloc()返回的内存/地址用于DMA。 可以将DMA映射到映射到vmalloc()区域中的内存，但这需要遍历页表以获取物理地址，然后使用诸如va()之类的东西将这些页面中的每一个转换回内核地址。 [编辑：当我们集成Gerd Knorr的通用代码来执行此操作时，请对此进行更新。 ]<br>

<br>This rule also means that you may use neither kernel image addresses (items in data/text/bss segments), nor module image addresses, nor stack addresses for DMA.  These could all be mapped somewhere entirely different than the rest of physical memory.  Even if those classes of memory could physically work with DMA, you'd need to ensure the I/O buffers were cacheline-aligned.  Without that, you'd see cacheline sharing problems (data corruption) on CPUs with DMA-incoherent caches. (The CPU could write to one word, DMA would write to a different one in the same cache line, and one of them could be overwritten.)<br>
<br>此规则还意味着您既不能使用内核映像地址(数据/文本/ bss段中的项)，也不能使用模块映像地址，也不能使用DMA的堆栈地址。 这些都可以映射到与其余物理内存完全不同的地方。 即使这些内存类别在物理上可以与DMA配合使用，您也需要确保I/O缓冲区与缓存行对齐。 否则，您将在具有DMA不相关缓存的CPU上看到缓存行共享问题(数据损坏)。 (CPU可以写入一个字，DMA可以写入同一高速缓存行中的另一个字，并且其中一个可以被覆盖。)<br>

<br>Also, this means that you cannot take the return of a kmap() call and DMA to/from that.  This is similar to vmalloc().<br>
<br>同样，这意味着您无法对kmap()的地址进行DMA映射。 这类似于vmalloc()。<br>

<br>What about block I/O and networking buffers?  The block I/O and networking subsystems make sure that the buffers they use are valid for you to DMA from/to.<br>
<br>块I / O和网络缓冲区呢？ 块I / O和网络子系统确保它们从DMA读取/写入使用的缓冲区有效。<br>

## DMA addressing capabilities DMA的寻址能力？
## =====================
<br>By default, the kernel assumes that your device can address 32-bits of DMA addressing.  For a 64-bit capable device, this needs to be increased, and for a device with limitations, it needs to be decreased.<br>

<br>默认情况下，内核假定您的设备可以寻址DMA的32位 寻址。对于支持64位的设备，此功能需要增加，对于 有局限性的设备，需要减少。<br>

<br>Special note about PCI: PCI-X specification requires PCI-X devices to support 64-bit addressing (DAC) for all transactions.  And at least one platform (SGI SN2) requires 64-bit consistent allocations to operate correctly when the IO bus is in PCI-X mode.<br>
<br>关于PCI的特别说明：PCI-X规范要求PCI-X设备支持64位寻址(DAC)。 当IO总线处于PCI-X模式时，至少有一个平台(SGI SN2)需要64位一致的分配才能正确运行。<br>
<br>For correct operation, you must set the DMA mask to inform the kernel about your devices DMA addressing capabilities.
<br>
<br>为了正确操作，您必须设置DMA掩码以通知内核有关 您的设备的DMA寻址功能。<br>
<br>This is performed via a call to dma_set_mask_and_coherent():<br>
<br>这是通过调用dma_set_mask_and_coherent()来执行的：<br>
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
<br>这里，dev是指向您设备的设备结构的指针，而mask是描述您设备支持的地址的哪些位的位掩码。 设备的设备结构通常嵌入在设备的总线特定设备结构中。 例如，＆pdev-> dev是指向PCI设备的设备结构的指针(pdev是指向设备的PCI设备结构的指针)。<br>

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
<br>相干一致性掩码始终和流式掩码一样，或者比流式掩码小。 但是，在极少数情况下，设备驱动程序仅使用一致的分配，则必须检查dma_set_coherent_mask()的返回值。<br>
<br>Finally, if your device can only drive the low 24-bits of address you might do something like:<br>
<br>最后，如果您的设备只能驱动低24位地址，则可以执行以下操作：<br>
```c
	if (dma_set_mask(dev, DMA_BIT_MASK(24))) {
		dev_warn(dev, "mydev: 24-bit DMA addressing not available\n");
		goto ignore_this_device;
	}
```
<br>When dma_set_mask() or dma_set_mask_and_coherent() is successful, and returns zero, the kernel saves away this mask you have provided. The kernel will use this information later when you make DMA mappings.<br>
<br>当dma_set_mask()或dma_set_mask_and_coherent()成功并返回零时，内核将保存您提供的该掩码。 稍后进行DMA映射时，内核将使用此信息。<br>

<br>There is a case which we are aware of at this time, which is worth mentioning in this documentation.  If your device supports multiple functions (for example a sound card provides playback and record functions) and the various different functions have _different_ DMA addressing limitations, you may wish to probe each mask and only provide the functionality which the machine can handle.  It is important that the last call to dma_set_mask() be for the most specific mask.<br>
<br>目前，我们知道一个案例，在本文档中值得一提。 如果您的设备支持多种功能(例如，声卡提供播放和录音功能)并且各种不同的功能都具有不同的DMA寻址限制，则您可能希望探查每个掩码，而仅提供机器可以处理的功能。 重要的是，对dma_set_mask()的最后一次调用应针对最特定的掩码<br>

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
<br>同样，在某些平台上，驱动程序可能需要刷新CPU写缓冲区的方式与刷新PCI桥中发现的写缓冲区的方式相同(例如，在写寄存器后读取寄存器的值)。<br>

<br>Streaming DMA mappings which are usually mapped for one DMA transfer, unmapped right after it (unless you use dma_sync_* below) and for which hardware can optimize for sequential accesses.<br>
<br>流式DMA通常映射为一个DMA传输，紧接其后是未映射的(除非您在下面使用dma_sync_ *)，并且硬件可以针对顺序访问进行优化。<br>

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
<br>To allocate and map large (PAGE_SIZE or so) consistent DMA regions, you should do:<br>
<br>申请并且映射大片的一致性DMA，你应该使用<br>
```c
	dma_addr_t dma_handle;

	cpu_addr = dma_alloc_coherent(dev, size, &dma_handle, gfp);
```
<br>where device is a ``struct device *``. This may be called in interrupt context with the GFP_ATOMIC flag.<br>
<br>其中device是``struct device *''。 这可以在带有GFP_ATOMIC标志的中断上下文中调用<br>

<br>Size is the length of the region you want to allocate, in bytes.<br>
<br>大小是您要分配的区域的长度，以字节为单位。<br>

<br>This routine will allocate RAM for that region, so it acts similarly to __get_free_pages() (but takes size instead of a page order).  If your driver needs regions sized smaller than a page, you may prefer using the dma_pool interface, described below.<br>
<br>该例程将为该区域分配RAM，因此其行为类似于__get_free_pages()(但采用大小而不是页面ordre)。 如果驱动程序需要的区域大小小于页面，则可能更喜欢使用dma_pool接口，如下所述。<br>

<br>The consistent DMA mapping interfaces, will by default return a DMA address which is 32-bit addressable.  Even if the device indicates (via the DMA mask) that it may address the upper 32-bits, consistent allocation will only return > 32-bit addresses for DMA if the consistent DMA mask has been explicitly changed via dma_set_coherent_mask().  This is true of the dma_pool interface as well.<br>
<br>默认情况下，一致的DMA映射接口将返回32位可寻址的DMA地址。 即使设备指示(通过DMA掩码)它可以寻址高32位，但如果已通过dma_set_coherent_mask()显式更改了一致的DMA掩码，则一致的分配将仅返回DMA的> 32位地址。 dma_pool接口也是如此。<br>

<br>dma_alloc_coherent() returns two values: the virtual address which you can use to access it from the CPU and dma_handle which you pass to the card.<br>
<br>dma_alloc_coherent()返回两个值：可用于从CPU访问它的虚拟地址和传递给设备卡的dma_handle。<br>

<br>The CPU virtual address and the DMA address are both guaranteed to be aligned to the smallest PAGE_SIZE order which is greater than or equal to the requested size.  This invariant exists (for example) to guarantee that if you allocate a chunk which is smaller than or equal to 64 kilobytes, the extent of the buffer you receive will not cross a 64K boundary.<br>
<br>保证CPU虚拟地址和DMA地址都与最小PAGE_SIZE顺序对齐，该最小PAGE_SIZE order大于或等于请求的大小。 该不变量存在(例如)以保证，如果您分配的块小于或等于64 KB，则接收到的缓冲区范围将不会超过64K边界。<br>

<br>To unmap and free such a DMA region, you call::<br>
<br>要取消映射并释放此类DMA区域，需要调用：：<br>
```c
	dma_free_coherent(dev, size, cpu_addr, dma_handle);
```
<br>where dev, size are the same as in the above call and cpu_addr and dma_handle are the values dma_alloc_coherent() returned to you. This function may not be called in interrupt context.<br>
<br>其中dev，size与上述调用中的相同，而cpu_addr和dma_handle是返回给您的dma_alloc_coherent()值。 不得在中断上下文中调用此函数。<br>

<br>If your driver needs lots of smaller memory regions, you can write custom code to subdivide pages returned by dma_alloc_coherent(), or you can use the dma_pool API to do that.  A dma_pool is like a kmem_cache, but it uses dma_alloc_coherent(), not __get_free_pages(). Also, it understands common hardware constraints for alignment, like queue heads needing to be aligned on N byte boundaries.<br>
<br>如果驱动程序需要很多较小的内存区域，则可以编写自定义代码来细分dma_alloc_coherent()返回的页面，或者可以使用dma_pool API来执行此操作。 dma_pool类似于kmem_cache，但是它使用dma_alloc_coherent()，而不是__get_free_pages()。 而且，它了解对齐的常见硬件限制，例如队列头需要在N个字节边界上对齐。<br>


<br>Create a dma_pool like this::<br>
创建一个dma_pool，如下所示：
```c
	struct dma_pool *pool;

	pool = dma_pool_create(name, dev, size, align, boundary);
```
<br>The "name" is for diagnostics (like a kmem_cache name); dev and size are as above.  The device's hardware alignment requirement for this type of data is "align" (which is expressed in bytes, and must be a power of two).  If your device has no boundary crossing restrictions, pass 0 for boundary; passing 4096 says memory allocated from this pool must not cross 4KByte boundaries (but at that time it may be better to use dma_alloc_coherent() directly instead).<br>
<br>“name”用于诊断(例如kmem_cache名称)； dev和大小如上。 设备对此类数据的硬件对齐要求是“对齐”(以字节表示，必须为2的幂)。 如果您的设备没有边界限制，则传递0作为边界。 传递4096表示从该池分配的内存一定不能越过4KB边界(但是此时最好直接使用dma_alloc_coherent()更好)。<br>

<br>Allocate memory from a DMA pool like this::<br>
<br>像这样从DMA池分配内存：<br>
```c
	cpu_addr = dma_pool_alloc(pool, flags, &dma_handle);
```
<br>flags are GFP_KERNEL if blocking is permitted (not in_interrupt nor holding SMP locks), GFP_ATOMIC otherwise.  Like dma_alloc_coherent(), this returns two values, cpu_addr and dma_handle.<br>
<br>如果允许阻止(不允许in_interrupt或不持有SMP锁)，则标志为GFP_KERNEL，否则为GFP_ATOMIC。 像dma_alloc_coherent()一样，它返回两个值cpu_addr和dma_handle。<br>

<br>Free memory that was allocated from a dma_pool like this::<br>
<br>像这样从dma_pool分配的可用内存：<br>
```c
	dma_pool_free(pool, cpu_addr, dma_handle);
```
<br>where pool is what you passed to dma_pool_alloc(), and cpu_addr and dma_handle are the values dma_pool_alloc() returned. This function may be called in interrupt context.<br>
<br>其中pool是您传递给dma_pool_alloc()的内容，而cpu_addr和dma_handle是返回的dma_pool_alloc()值。 可以在中断上下文中调用此函数。<br>

<br>Destroy a dma_pool by calling::<br>
<br>通过调用销毁dma_pool<br>
```c
	dma_pool_destroy(pool);
```
<br>Make sure you've called dma_pool_free() for all memory allocated from a pool before you destroy the pool. This function may not be called in interrupt context.<br>
<br>在销毁池之前，请确保已为从池分配的所有内存调用dma_pool_free()。 不得在中断上下文中调用此函数。<br>

## DMA Direction DMA的方向
## =============

<br>The interfaces described in subsequent portions of this document take a DMA direction argument, which is an integer and takes on one of the following values::<br>
<br>本文档后续部分中描述的接口采用DMA方向参数，该参数是整数，并具有以下值之一：<br>
```c
 DMA_BIDIRECTIONAL
 DMA_TO_DEVICE
 DMA_FROM_DEVICE
 DMA_NONE
```
<br>You should provide the exact DMA direction if you know it.<br>
<br>如果知道，则应提供确切的DMA方向<br>

<br>DMA_TO_DEVICE means "from main memory to the device" DMA_FROM_DEVICE means "from the device to main memory" It is the direction in which the data moves during the DMA transfer.<br>
<br>DMA_TO_DEVICE表示“从主存储器到设备” DMA_FROM_DEVICE表示“从设备到主存储器”这是DMA传输期间数据移动的方向。<br>

<br>You are _strongly_ encouraged to specify this as precisely as you possibly can.<br>
<br>强烈建议您尽可能精确地指定此名称<br>

<br>If you absolutely cannot know the direction of the DMA transfer, specify DMA_BIDIRECTIONAL.  It means that the DMA can go in either direction.  The platform guarantees that you may legally specify this, and that it will work, but this may be at the cost of performance for example.<br>
<br>如果您完全不知道DMA传输的方向，请指定DMA_BIDIRECTIONAL。 这意味着DMA可以沿任一方向运行。 该平台保证您可以合法地指定它并且可以使用，但是这可能会降低性能。<br>

<br>The value DMA_NONE is to be used for debugging.  One can hold this in a data structure before you come to know the precise direction, and this will help catch cases where your direction tracking logic has failed to set things up properly.<br>
<br>值DMA_NONE将用于调试。 在您知道准确的方向之前，可以将其保存在数据结构中，这将有助于发现方向跟踪逻辑未能正确设置事物的情况<br>

<br>Another advantage of specifying this value precisely (outside of potential platform-specific optimizations of such) is for debugging. Some platforms actually have a write permission boolean which DMA mappings can be marked with, much like page protections in the user program address space.  Such platforms can and do report errors in the kernel logs when the DMA controller hardware detects violation of the permission setting.<br>
<br>精确指定此值的另一个好处(在此类特定平台的潜在优化之外)是用于调试。 某些平台实际上具有写许可权布尔值，可以用DMA映射标记该布尔值，就像用户程序地址空间中的页面保护一样。 当DMA控制器硬件检测到违反权限设置时，此类平台可以并且确实在内核日志中报告错误。<br>

<br>Only streaming mappings specify a direction, consistent mappings implicitly have a direction attribute setting of DMA_BIDIRECTIONAL.<br>
<br>只有流映射指定方向，一致映射隐式具有DMA_BIDIRECTIONAL的方向属性设置。<br>

<br>The SCSI subsystem tells you the direction to use in the 'sc_data_direction' member of the <br>SCSI command your driver is working on.<br>
SCSI子系统在驱动程序正在使用的SCSI命令的'sc_data_direction'成员中告诉您使用的方向。<br>

<br>For Networking drivers, it's a rather simple affair.  For transmit packets, map/unmap them with the DMA_TO_DEVICE direction specifier.  For receive packets, just the opposite, map/unmap them with the DMA_FROM_DEVICE direction specifier.<br>
<br>对于网络驱动程序，这是一件相当简单的事情。 对于发送数据包，请使用DMA_TO_DEVICE方向说明符映射/取消映射它们。 对于接收数据包，正好相反，请使用DMA_FROM_DEVICE方向说明符映射/取消映射它们<br>


## Using Streaming DMA mappings 使用流式DMA映射
## ============================
<br>The streaming DMA mapping routines can be called from interrupt context.  There are two versions of each map/unmap, one which will map/unmap a single memory region, and one which will map/unmap a scatterlist.<br>
<br>流式DMA映射例程可以从中断上下文中调用。 每个映射/取消映射有两种版本，一种将映射/取消映射单个内存区域，另一种将映射/取消映射散列表。<br>

<br>To map a single region, you do::<br>
<br>要映射单个区域，请执行以下操作：<br>
```c
	struct device *dev = &my_dev->dev;
	dma_addr_t dma_handle;
	void *addr = buffer->ptr;
	size_t size = buffer->len;

	dma_handle = dma_map_single(dev, addr, size, direction);
	if (dma_mapping_error(dev, dma_handle)) {
		/*
		 * reduce current DMA mapping usage,
		 * delay and try again later or
		 * reset driver.
		 */
		goto map_error_handling;
	}
```
<br>and to unmap it::<br>
```c
	dma_unmap_single(dev, dma_handle, size, direction);
```
<br>You should call dma_mapping_error() as dma_map_single() could fail and return error.  Doing so will ensure that the mapping code will work correctly on all DMA implementations without any dependency on the specifics of the underlying implementation. Using the returned address without checking for errors could result in failures ranging from panics to silent data corruption.  The same applies to dma_map_page() as well.<br>
<br>您应该调用dma_mapping_error()，因为dma_map_single()可能会失败并返回错误。 这样做将确保映射代码将在所有DMA实现上正常工作，而无需依赖底层实现的细节。 使用返回的地址而不检查错误可能导致失败，从panic到无声数据损坏。 dma_map_page()也一样。<br>

<br>You should call dma_unmap_single() when the DMA activity is finished, e.g., from the interrupt which told you that the DMA transfer is done.<br>
<br>当DMA活动结束时，您应该调用dma_unmap_single()例如从终端得知DMA传输结束了。<br>

<br>Using CPU pointers like this for single mappings has a disadvantage: you cannot reference HIGHMEM memory in this way.  Thus, there is a map/unmap interface pair akin to dma_{map,unmap}_single().  These interfaces deal with page/offset pairs instead of CPU pointers.<br>
<br>对单个映射使用这样的CPU指针有一个缺点：您不能以这种方式引用HIGHMEM内存。 因此，存在类似于dma_ {map，unmap} _single()的map / unmap接口对。 这些接口处理页面/偏移对，而不是CPU指针。<br>

<br>Specifically::<br>
```c
	struct device *dev = &my_dev->dev;
	dma_addr_t dma_handle;
	struct page *page = buffer->page;
	unsigned long offset = buffer->offset;
	size_t size = buffer->len;

	dma_handle = dma_map_page(dev, page, offset, size, direction);
	if (dma_mapping_error(dev, dma_handle)) {
		/*
		 * reduce current DMA mapping usage,
		 * delay and try again later or
		 * reset driver.
		 */
		goto map_error_handling;
	}

	...

	dma_unmap_page(dev, dma_handle, size, direction);
```
<br>Here, "offset" means byte offset within the given page.<br>
<br>此处，“偏移量”是指给定页面内的字节偏移量<br>

<br>You should call dma_mapping_error() as dma_map_page() could fail and return error as outlined under the dma_map_single() discussion.<br>
<br>您应该调用dma_mapping_error()，因为dma_map_page()可能会失败并返回dma_map_single()讨论中概述的错误。<br>

<br>You should call dma_unmap_page() when the DMA activity is finished, e.g., from the interrupt which told you that the DMA transfer is done.<br>
<br>当DMA活动完成时，例如，从告诉您DMA传输已完成的中断中，您应该调用dma_unmap_page()。<br>

<br>With scatterlists, you map a region gathered from several regions by::<br>
```c
	int i, count = dma_map_sg(dev, sglist, nents, direction);
	struct scatterlist *sg;

	for_each_sg(sglist, sg, count, i) {
		hw_address[i] = sg_dma_address(sg);
		hw_len[i] = sg_dma_len(sg);
	}
```
<br>where nents is the number of entries in the sglist.<br>
<br>其中nents是sglist中条目的数量。<br>

<br>The implementation is free to merge several consecutive sglist entries into one (e.g. if DMA mapping is done with PAGE_SIZE granularity, any consecutive sglist entries can be merged into one provided the first one ends and the second one starts on a page boundary - in fact this is a huge advantage for cards which either cannot do scatter-gather or have very limited number of scatter-gather entries) and returns the actual number of sg entries it mapped them to. On failure 0 is returned.<br>
<br>该实现可以自由地将几个连续的sglist条目合并为一个(例如，如果DMA映射是用PAGE_SIZE粒度完成的，则任何连续的sglist条目都可以合并为一个，前提是第一个结束并且第二个在页面边界开始- 实际上，这对于不能执行散点收集或散点收集条目数量非常有限并返回其映射到的sg条目的实际数量的卡来说，是一个巨大的优势。 失败时返回0。<br>

<br>Then you should loop count times (note: this can be less than nents times) and use sg_dma_address() and sg_dma_len() macros where you previously accessed sg->address and sg->length as shown above.<br>
<br>然后，您应该循环计数次数(注意：这可以少于nents次数)，并使用sg_dma_address()和sg_dma_len()宏，您以前在其中访问过sg-> address和sg-> length，如上所示。

<br>To unmap a scatterlist, just call::<br>
<br>要取消散列表的映射，只需致电：: <br>
```c
	dma_unmap_sg(dev, sglist, nents, direction);
```
<br>Again, make sure DMA activity has already finished.<br>
<br>同样，请确保DMA活动已经完成。<br>

.. note:

	The 'nents' argument to the dma_unmap_sg call must be the _same_ one you passed into the dma_map_sg call,
	it should _NOT_ be the 'count' value _returned_ from the dma_map_sg call.
	dma_unmap_sg调用的“ nents”参数必须是您传递给dma_map_sg调用的_same_，它应该_NOT_是dma_map_sg调用中的“计数”值_returned_。

<br>Every dma_map_{single,sg}() call should have its dma_unmap_{single,sg}() counterpart, because the DMA address space is a shared resource and you could render the machine unusable by consuming all DMA addresses.<br>
<br>每个dma_map_ {single，sg}()调用都应有其dma_unmap_ {single，sg}()对应物，因为DMA地址空间是共享资源，您可以通过使用所有DMA地址来使计算机不可用。<br >

<br>If you need to use the same streaming DMA region multiple times and touch the data in between the DMA transfers, the buffer needs to be synced properly in order for the CPU and device to see the most up-to-date and correct copy of the DMA buffer.<br>
<br>如果您需要多次使用相同的流DMA区域并触摸DMA传输之间的数据，则需要正确同步缓冲区，以便CPU和设备看到最新和正确的DMA缓冲区的副本的信息。<br>

<br>So, firstly, just map it with dma_map_{single,sg}(), and after each DMA transfer call either::<br>
```c
	dma_sync_single_for_cpu(dev, dma_handle, size, direction);
```
or:
```c
	dma_sync_sg_for_cpu(dev, sglist, nents, direction);
```
<br>as appropriate.<br>

<br>Then, if you wish to let the device get at the DMA area again, finish accessing the data with the CPU, and then before actually giving the buffer to the hardware call either::<br>
<br>然后，如果您希望让设备再次进入DMA区域，请先使用CPU完成访问数据，然后再将缓冲区实际分配给硬件调用：
```c
	dma_sync_single_for_device(dev, dma_handle, size, direction);
```
or:
```c
	dma_sync_sg_for_device(dev, sglist, nents, direction);
```
as appropriate.

.. note::

	The 'nents' argument to dma_sync_sg_for_cpu() and dma_sync_sg_for_device() must be the same passed to dma_map_sg().
	It is _NOT_ the count returned by dma_map_sg().
	dma_sync_sg_for_cpu()和dma_sync_sg_for_device()的“ nents”参数必须与传递给dma_map_sg()的参数相同。不是dma_map_sg()返回的计数。

<br>After the last DMA transfer call one of the DMA unmap routines dma_unmap_{single,sg}(). If you don't touch the data from the first dma_map_*() call till dma_unmap_*(), then you don't have to call the dma_sync_*() routines at all.<br>
<br>在最后一次DMA传输调用之后，其中一个DMA取消映射例程dma_unmap_ {single，sg}()。 如果您不触摸第一个dma_map _ *()调用中的数据，直到dma_unmap _ *()，则根本不必调用dma_sync _ *()例程。<br>

<br>Here is pseudo code which shows a situation in which you would need to use the dma_sync_*() interfaces::<br>
<br>这是伪代码，它显示了您需要使用dma_sync _ *()接口的情况：: <br>
```c
	my_card_setup_receive_buffer(struct my_card *cp, char *buffer, int len)
	{
		dma_addr_t mapping;

		mapping = dma_map_single(cp->dev, buffer, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(cp->dev, mapping)) {
			/*
			 * reduce current DMA mapping usage,
			 * delay and try again later or
			 * reset driver.
			 */
			goto map_error_handling;
		}

		cp->rx_buf = buffer;
		cp->rx_len = len;
		cp->rx_dma = mapping;

		give_rx_buf_to_card(cp);
	}

	...

	my_card_interrupt_handler(int irq, void *devid, struct pt_regs *regs)
	{
		struct my_card *cp = devid;

		...
		if (read_card_status(cp) == RX_BUF_TRANSFERRED) {
			struct my_card_header *hp;

			/* Examine the header to see if we wish
			 * to accept the data.  But synchronize
			 * the DMA transfer with the CPU first
			 * so that we see updated contents.
			 */
			dma_sync_single_for_cpu(&cp->dev, cp->rx_dma,
						cp->rx_len,
						DMA_FROM_DEVICE);

			/* Now it is safe to examine the buffer. */
			hp = (struct my_card_header *) cp->rx_buf;
			if (header_is_ok(hp)) {
				dma_unmap_single(&cp->dev, cp->rx_dma, cp->rx_len,
						 DMA_FROM_DEVICE);
				pass_to_upper_layers(cp->rx_buf);
				make_and_setup_new_rx_buf(cp);
			} else {
				/* CPU should not write to
				 * DMA_FROM_DEVICE-mapped area,
				 * so dma_sync_single_for_device() is
				 * not needed here. It would be required
				 * for DMA_BIDIRECTIONAL mapping if
				 * the memory was modified.
				 */
				give_rx_buf_to_card(cp);
			}
		}
	}
```
<br>Drivers converted fully to this interface should not use virt_to_bus() any longer, nor should they use bus_to_virt(). Some drivers have to be changed a little bit, because there is no longer an equivalent to bus_to_virt() in the dynamic DMA mapping scheme - you have to always store the DMA addresses returned by the dma_alloc_coherent(), dma_pool_alloc(), and dma_map_single() calls (dma_map_sg() stores them in the scatterlist itself if the platform supports dynamic DMA mapping in hardware) in your driver structures and/or in the card registers.<br>
<br>完全转换为该接口的驱动程序不应再使用virt_to_bus()，也不应使用bus_to_virt()。 一些驱动程序必须稍作更改，因为动态DMA映射方案中不再有与bus_to_virt()等效的驱动程序-您必须始终存储dma_alloc_coherent()，dma_pool_alloc()和dma_map_single()返回的DMA地址。)调用(如果平台支持硬件中的动态DMA映射，则dma_map_sg()将它们存储在分散列表本身中)在驱动程序结构和/或卡寄存器中。<br>

<br>All drivers should be using these interfaces with no exceptions.  It is planned to completely remove virt_to_bus() and bus_to_virt() as they are entirely deprecated.  Some ports already do not provide these as it is impossible to correctly support them.<br>
<br>所有驱动程序都应使用这些接口，没有例外。 由于完全不建议使用virt_to_bus()和bus_to_virt()，因此计划将其删除。 一些端口已经不提供这些功能，因为不可能正确地支持它们。<br>

## Handling Errors  错误处理
## ===============

<br>DMA address space is limited on some architectures and an allocation failure can be determined by:<br>
<br> DMA地址空间在某些体系结构上受到限制，分配失败可以通过以下方法确定：<br>

- checking if dma_alloc_coherent() returns NULL or dma_map_sg returns 0 检查dma_alloc_coherent()返回NULL还是dma_map_sg返回0

- checking the dma_addr_t returned from dma_map_single() and dma_map_page()
  by using dma_mapping_error():: 检查从dma_map_single()和dma_map_page()返回的dma_addr_t
   通过使用dma_mapping_error()：：
```c
	dma_addr_t dma_handle;

	dma_handle = dma_map_single(dev, addr, size, direction);
	if (dma_mapping_error(dev, dma_handle)) {
		/*
		 * reduce current DMA mapping usage,
		 * delay and try again later or
		 * reset driver.
		 */
		goto map_error_handling;
	}
```
- unmap pages that are already mapped, when mapping error occurs in the middle
  of a multiple page mapping attempt. These example are applicable to
  dma_map_page() as well. 当中间出现映射错误时，取消映射已映射的页面多页映射尝试。 这些示例适用于 以及dma_map_page()。
```c
Example 1::

	dma_addr_t dma_handle1;
	dma_addr_t dma_handle2;

	dma_handle1 = dma_map_single(dev, addr, size, direction);
	if (dma_mapping_error(dev, dma_handle1)) {
		/*
		 * reduce current DMA mapping usage,
		 * delay and try again later or
		 * reset driver.
		 */
		goto map_error_handling1;
	}
	dma_handle2 = dma_map_single(dev, addr, size, direction);
	if (dma_mapping_error(dev, dma_handle2)) {
		/*
		 * reduce current DMA mapping usage,
		 * delay and try again later or
		 * reset driver.
		 */
		goto map_error_handling2;
	}

	...

	map_error_handling2:
		dma_unmap_single(dma_handle1);
	map_error_handling1:

Example 2::

	/*
	 * if buffers are allocated in a loop, unmap all mapped buffers when
	 * mapping error is detected in the middle
	 */

	dma_addr_t dma_addr;
	dma_addr_t array[DMA_BUFFERS];
	int save_index = 0;

	for (i = 0; i < DMA_BUFFERS; i++) {

		...

		dma_addr = dma_map_single(dev, addr, size, direction);
		if (dma_mapping_error(dev, dma_addr)) {
			/*
			 * reduce current DMA mapping usage,
			 * delay and try again later or
			 * reset driver.
			 */
			goto map_error_handling;
		}
		array[i].dma_addr = dma_addr;
		save_index++;
	}

	...

	map_error_handling:

	for (i = 0; i < save_index; i++) {

		...

		dma_unmap_single(array[i].dma_addr);
	}
```
<br>Networking drivers must call dev_kfree_skb() to free the socket buffer and return NETDEV_TX_OK if the DMA mapping fails on the transmit hook (ndo_start_xmit). This means that the socket buffer is just dropped in the failure case.<br>
<br>网络驱动程序必须调用dev_kfree_skb()来释放套接字缓冲区，如果DMA映射在传输挂钩(ndo_start_xmit)失败，则返回NETDEV_TX_OK。 这意味着在发生故障的情况下套接字缓冲区将被丢弃。<br>

<br>SCSI drivers must return SCSI_MLQUEUE_HOST_BUSY if the DMA mapping fails in the queuecommand hook. This means that the SCSI subsystem passes the command to the driver again later.<br>
<br>如果queuecommand挂接中的DMA映射失败，则SCSI驱动程序必须返回SCSI_MLQUEUE_HOST_BUSY。 这意味着SCSI子系统稍后将命令再次传递给驱动程序。<br>

## Optimizing Unmap State Space Consumption 优化未映射状态空间消耗
## ========================================
<br>On many platforms, dma_unmap_{single,page}() is simply a nop. Therefore, keeping track of the mapping address and length is a waste of space.  Instead of filling your drivers up with ifdefs and the like to "work around" this (which would defeat the whole purpose of a portable API) the following facilities are provided.<br>
<br>在许多平台上，dma_unmap_ {single，page}()只是nop。 因此，跟踪映射地址和长度是浪费空间。 提供了以下功能，而不是为驱动程序添加ifdefs之类的东西以“解决”此问题(这会破坏便携式API的全部目的)。<br>

<br>Actually, instead of describing the macros one by one, we'll transform some example code.<br>
<br>实际上，我们将不对宏进行逐一描述，而是将一些示例代码转换为<br>。

1) Use DEFINE_DMA_UNMAP_{ADDR,LEN} in state saving structures.
```c
   Example, before::

	struct ring_state {
		struct sk_buff *skb;
		dma_addr_t mapping;
		__u32 len;
	};

   after::

	struct ring_state {
		struct sk_buff *skb;
		DEFINE_DMA_UNMAP_ADDR(mapping);
		DEFINE_DMA_UNMAP_LEN(len);
	};
```
2) Use dma_unmap_{addr,len}_set() to set these values.
```c
   Example, before::

	ringp->mapping = FOO;
	ringp->len = BAR;

   after::

	dma_unmap_addr_set(ringp, mapping, FOO);
	dma_unmap_len_set(ringp, len, BAR);
```
3) Use dma_unmap_{addr,len}() to access these values.
```c
   Example, before::

	dma_unmap_single(dev, ringp->mapping, ringp->len,
			 DMA_FROM_DEVICE);

   after::

	dma_unmap_single(dev,
			 dma_unmap_addr(ringp, mapping),
			 dma_unmap_len(ringp, len),
			 DMA_FROM_DEVICE);
```
<br>It really should be self-explanatory.  We treat the ADDR and LEN separately, because it is possible for an implementation to only need the address in order to perform the unmap operation.<br>
<br>这确实应该是不言自明的。 我们将ADDR和LEN分开对待，因为实现可能只需要地址即可执行取消映射操作。<br>

## Platform Issues 平台的问题
## ===============

<br>If you are just writing drivers for Linux and do not maintain an architecture port for the kernel, you can safely skip down to "Closing".<br>
<br>如果您只是为Linux编写驱动程序，并且不维护内核的体系结构端口，则可以安全地跳至“closing”。<br>

1) Struct scatterlist requirements. 结构散列表的要求。

   You need to enable CONFIG_NEED_SG_DMA_LENGTH if the architecture supports IOMMUs (including software IOMMU). <br>
   <br>如果体系结构支持IOMMU(包括软件IOMMU)，则需要启用CONFIG_NEED_SG_DMA_LENGTH。<br>

2) ARCH_DMA_MINALIGN

   Architectures must ensure that kmalloc'ed buffer is DMA-safe. Drivers and subsystems depend on it. If an architecture isn't fully DMA-coherent (i.e. hardware doesn't ensure that data in the CPU cache is identical to data in main memory), ARCH_DMA_MINALIGN must be set so that the memory allocator makes sure that kmalloc'ed buffer doesn't share a cache line with the others. See arch/arm/include/asm/cache.h as an example.<br>
   <br>架构必须确保kmalloc的缓冲区是DMA安全的。 驱动程序和子系统依赖于此。 如果架构不是完全与DMA一致的(即硬件不能确保CPU缓存中的数据与主内存中的数据相同)，则必须设置ARCH_DMA_MINALIGN，以便内存分配器确保kmalloc的缓冲区不 与其他人共享一条缓存行。 请参阅arch/arm/include/asm/cache.h作为示例。<br>

   <br>Note that ARCH_DMA_MINALIGN is about DMA memory alignment constraints. You don't need to worry about the architecture data alignment constraints (e.g. the alignment constraints about 64-bit objects).<br>
   <br>请注意，ARCH_DMA_MINALIGN与DMA内存对齐约束有关。 您无需担心架构数据对齐约束(例如，有关64位对象的对齐约束)。<br>

## Closing
## =======
<br>This document, and the API itself, would not be in its curren form without the feedback and suggestions from numerous individuals We would like to specifically mention, in no particular order, the following people::<br>
<br>如果没有众多个人的反馈和建议，本文档以及API本身就不会是目前的形式。我们要特别提及以下人员：<br>

	Russell King <rmk@arm.linux.org.uk>
	Leo Dagum <dagum@barrel.engr.sgi.com>
	Ralf Baechle <ralf@oss.sgi.com>
	Grant Grundler <grundler@cup.hp.com>
	Jay Estabrook <Jay.Estabrook@compaq.com>
	Thomas Sailer <sailer@ife.ee.ethz.ch>
	Andrea Arcangeli <andrea@suse.de>
	Jens Axboe <jens.axboe@oracle.com>
	David Mosberger-Tang <davidm@hpl.hp.com>