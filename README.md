part-y
======

Partitioning and format utility for Linux and MS Windows
--------------------------------------------------------

part-y (read '_part-why_' or just '_party_') is a combined Linux or MS Windows tool, respectively.

**This is work under progress, the current version is 0.4-alpha.**

The currently available version of this tool is mainly here for the purpose of converting
Windows 10 machines running with an MBR-style partitioning (BIOS boot mode) into GPT-partitioned systems,
which can perform (secure) UEFI boots.

Microsoft provides a tool called _mbr2gpt.exe_ to convert an MBR-partitioned system to a GPT-partitioned system (GUID Partition Table)
including all required steps (modification of Boot Configuration Data, etc.) to enable a (secure) UEFI boot. This MS tool can only
convert very specific partitionings (only primary partitions, only three partitions in MBR, etc.).

**part-y** claims to perform this conversion on the vast majority of existing MBR-partitioned systems. Give it a try!

**Please DO read the accompanying PDF document 'step-by-step-guide.pdf' in the docs folder.**

DISCLAIMER
==========
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
