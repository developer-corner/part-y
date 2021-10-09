## part-y (or 'part-why' or just 'party')

part-y is a partitioning and format tool for MS Windows or Linux, respectively. Yes, there are already a lot of partitioning tools around. The main reasons for providing part-y are:

- I wanted to learn everything about MBR (Master Boot Record) versus GPT (GUID Partition Table)
- the MS provided tool mbr2gpt.exe was unable to convert my Windows 10 machines from legacy BIOS boot to (secure) UEFI boot
- for my work, I needed a tool which can create full partitionings with automatic formatting in image files, which I can dump to e.g. USB sticks as a whole; everything should be provided on the command line, no user interaction at all
- BCD (Boot Configuration Data) stores were always a big secret until I invented some of the internal mechanisms (in the binary registry hives); full source code is provided to build a BCD store using Microsoft's WMI provider from scratch
- EFI NVRAM variables were also investigated (using the current UEFI specification, v2.9) to learn more about 'load options'
- part-y is still _work under progress_ but the conversion facility for Windows 10 machines (MBR to GPT) is fully operational

### Documentation

There is some preliminary PDF documentation (step by step guide) available here: https://github.com/developer-corner/part-y/blob/main/docs/step-by-step-guide.pdf

### License

MIT

### Disclaimer

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

