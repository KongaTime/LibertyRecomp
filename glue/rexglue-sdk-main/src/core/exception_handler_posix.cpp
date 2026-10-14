/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/exception_handler.h>

#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC || REX_PLATFORM_PS5

#include <signal.h>

#include <cstdint>
#include <cstring>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/platform.h>

#include <ucontext.h>

#if REX_PLATFORM_PS5
// FreeBSD's amd64 machine context (mc_rip, mc_err, an fxsave image in
// mc_fpstate), at the place the console puts it: the payload SDK fork's
// sys/_ucontext.h carries the console's extra 48 bytes, and context.h refuses
// to build against a header without them.
#include <machine/fpu.h>

#include <ps5platform/context.h>
#include <ps5platform/kernel.h>

#include <rex/platform/fault_prospero.h>
#endif

namespace rex::arch {

bool signal_handlers_installed_ = false;
struct sigaction original_sigill_handler_;
struct sigaction original_sigsegv_handler_;

#if REX_PLATFORM_PS5
// The console maps a title's code execute-only: reading it ends the title at
// once (SYSTEM_XO_VIOLATION), with no signal. MMIOHandler reads the instruction
// that faulted, so the code segment is made readable before any handler runs.
// PS5_EdgeOfTimeTitle's code read check (2026-10-12): sceKernelMprotect
// read+execute on a page of the title's code made it readable and it still ran,
// while untouched code ended the title when read. sceKernelQueryMemoryProtection
// answered 0x4 (execute) for the page before and after, so it cannot say whether
// code is readable; it gives the segment's bounds (0x400000 up in that title).
// System modules' code stays execute-only. The segment's bounds go to the fault
// records (rex/platform/fault_prospero.h), which translate addresses in it.
static void MakeCodeReadable() {
  void* start = nullptr;
  void* end = nullptr;
  uint32_t protection = 0;
  const int32_t query = sceKernelQueryMemoryProtection(
      reinterpret_cast<void*>(&MakeCodeReadable), &start, &end, &protection);
  if (query != 0) {
    REXLOG_ERROR("PS5: no code segment to make readable (query {:#010x})", static_cast<uint32_t>(query));
    platform::InitializeProsperoFaultDiagnostics(0, 0);
    return;
  }
  platform::InitializeProsperoFaultDiagnostics(reinterpret_cast<uint64_t>(start),
                                               reinterpret_cast<uint64_t>(end));
  const size_t length = static_cast<size_t>(static_cast<uint8_t*>(end) - static_cast<uint8_t*>(start));
  const int32_t result =
      sceKernelMprotect(start, length, PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_EXEC);
  if (result != 0) {
    REXLOG_ERROR("PS5: code at {}-{} stays execute-only ({:#010x}); an MMIO fault will end the title",
                 start, end, static_cast<uint32_t>(result));
    return;
  }
  REXLOG_INFO("PS5: code at {}-{} made readable for the MMIO handler", start, end);
}

// The XMM registers in the fxsave image FreeBSD keeps in mc_fpstate.
static vec128_t* Ps5XmmRegisters(mcontext_t& mcontext) {
  static_assert(sizeof(mcontext.mc_fpstate) >= sizeof(struct savefpu));
  return reinterpret_cast<vec128_t*>(
      reinterpret_cast<struct savefpu*>(mcontext.mc_fpstate)->sv_xmm);
}
#endif

// This can be as large as needed, but isn't often needed.
// As we will be sometimes firing many exceptions we want to avoid having to
// scan the table too much or invoke many custom handlers.
constexpr size_t kMaxHandlerCount = 8;

// All custom handlers, left-aligned and null terminated.
// Executed in order.
std::pair<ExceptionHandler::Handler, void*> handlers_[kMaxHandlerCount];

#if REX_PLATFORM_PS5
// A fault no handler took: recorded in fault.log, then given to the handler
// that was there before ReXGlue's, or to the default, so the title ends rather
// than faulting on the same instruction forever. From TheSimpsonsGame-PS5-
// Builder's exception_handler_prospero.cpp (ps5/PORTED.md), which records only
// on the default disposition; here the record comes first either way.
static void ForwardUnhandledException(int signal_number, siginfo_t* signal_info,
                                      void* signal_context) {
  platform::ReportProsperoFatalSignal(signal_number, signal_info, signal_context);
  const struct sigaction& previous =
      signal_number == SIGILL ? original_sigill_handler_ : original_sigsegv_handler_;
  if (previous.sa_handler == SIG_IGN) return;
  if (previous.sa_handler != SIG_DFL && previous.sa_handler != nullptr) {
    if (previous.sa_flags & SA_SIGINFO)
      previous.sa_sigaction(signal_number, signal_info, signal_context);
    else
      previous.sa_handler(signal_number);
    return;
  }
  sigaction(signal_number, &previous, nullptr);
  raise(signal_number);
}
#endif

static void ExceptionHandlerCallback(int signal_number, siginfo_t* signal_info,
                                     void* signal_context) {
  mcontext_t& mcontext = reinterpret_cast<ucontext_t*>(signal_context)->uc_mcontext;

  HostThreadContext thread_context;

#if REX_ARCH_AMD64
#if REX_PLATFORM_PS5
  thread_context.rip = uint64_t(mcontext.mc_rip);
  thread_context.eflags = uint32_t(mcontext.mc_rflags);
  thread_context.rax = uint64_t(mcontext.mc_rax);
  thread_context.rcx = uint64_t(mcontext.mc_rcx);
  thread_context.rdx = uint64_t(mcontext.mc_rdx);
  thread_context.rbx = uint64_t(mcontext.mc_rbx);
  thread_context.rsp = uint64_t(mcontext.mc_rsp);
  thread_context.rbp = uint64_t(mcontext.mc_rbp);
  thread_context.rsi = uint64_t(mcontext.mc_rsi);
  thread_context.rdi = uint64_t(mcontext.mc_rdi);
  thread_context.r8 = uint64_t(mcontext.mc_r8);
  thread_context.r9 = uint64_t(mcontext.mc_r9);
  thread_context.r10 = uint64_t(mcontext.mc_r10);
  thread_context.r11 = uint64_t(mcontext.mc_r11);
  thread_context.r12 = uint64_t(mcontext.mc_r12);
  thread_context.r13 = uint64_t(mcontext.mc_r13);
  thread_context.r14 = uint64_t(mcontext.mc_r14);
  thread_context.r15 = uint64_t(mcontext.mc_r15);
  std::memcpy(thread_context.xmm_registers, Ps5XmmRegisters(mcontext),
              sizeof(thread_context.xmm_registers));
#else
  thread_context.rip = uint64_t(mcontext.gregs[REG_RIP]);
  thread_context.eflags = uint32_t(mcontext.gregs[REG_EFL]);
  // The REG_ order may be different than the register indices in the
  // instruction encoding.
  thread_context.rax = uint64_t(mcontext.gregs[REG_RAX]);
  thread_context.rcx = uint64_t(mcontext.gregs[REG_RCX]);
  thread_context.rdx = uint64_t(mcontext.gregs[REG_RDX]);
  thread_context.rbx = uint64_t(mcontext.gregs[REG_RBX]);
  thread_context.rsp = uint64_t(mcontext.gregs[REG_RSP]);
  thread_context.rbp = uint64_t(mcontext.gregs[REG_RBP]);
  thread_context.rsi = uint64_t(mcontext.gregs[REG_RSI]);
  thread_context.rdi = uint64_t(mcontext.gregs[REG_RDI]);
  thread_context.r8 = uint64_t(mcontext.gregs[REG_R8]);
  thread_context.r9 = uint64_t(mcontext.gregs[REG_R9]);
  thread_context.r10 = uint64_t(mcontext.gregs[REG_R10]);
  thread_context.r11 = uint64_t(mcontext.gregs[REG_R11]);
  thread_context.r12 = uint64_t(mcontext.gregs[REG_R12]);
  thread_context.r13 = uint64_t(mcontext.gregs[REG_R13]);
  thread_context.r14 = uint64_t(mcontext.gregs[REG_R14]);
  thread_context.r15 = uint64_t(mcontext.gregs[REG_R15]);
  std::memcpy(thread_context.xmm_registers, mcontext.fpregs->_xmm,
              sizeof(thread_context.xmm_registers));
#endif  // REX_PLATFORM_PS5
#elif REX_ARCH_ARM64
  std::memcpy(thread_context.x, mcontext.regs, sizeof(thread_context.x));
  thread_context.sp = mcontext.sp;
  thread_context.pc = mcontext.pc;
  thread_context.pstate = mcontext.pstate;
  struct fpsimd_context* mcontext_fpsimd = nullptr;
  struct esr_context* mcontext_esr = nullptr;
  for (struct _aarch64_ctx* mcontext_extension =
           reinterpret_cast<struct _aarch64_ctx*>(mcontext.__reserved);
       mcontext_extension->magic;
       mcontext_extension = reinterpret_cast<struct _aarch64_ctx*>(
           reinterpret_cast<uint8_t*>(mcontext_extension) + mcontext_extension->size)) {
    switch (mcontext_extension->magic) {
      case FPSIMD_MAGIC:
        mcontext_fpsimd = reinterpret_cast<struct fpsimd_context*>(mcontext_extension);
        break;
      case ESR_MAGIC:
        mcontext_esr = reinterpret_cast<struct esr_context*>(mcontext_extension);
        break;
      default:
        break;
    }
  }
  assert_not_null(mcontext_fpsimd);
  if (mcontext_fpsimd) {
    thread_context.fpsr = mcontext_fpsimd->fpsr;
    thread_context.fpcr = mcontext_fpsimd->fpcr;
    std::memcpy(thread_context.v, mcontext_fpsimd->vregs, sizeof(thread_context.v));
  }
#endif  // REX_ARCH

  Exception ex;
  switch (signal_number) {
    case SIGILL:
      ex.InitializeIllegalInstruction(&thread_context);
      break;
    case SIGSEGV: {
      Exception::AccessViolationOperation access_violation_operation;
#if REX_ARCH_AMD64
      // x86_pf_error_code::X86_PF_WRITE
      constexpr uint64_t kX86PageFaultErrorCodeWrite = UINT64_C(1) << 1;
#if REX_PLATFORM_PS5
      access_violation_operation = (uint64_t(mcontext.mc_err) & kX86PageFaultErrorCodeWrite)
                                       ? Exception::AccessViolationOperation::kWrite
                                       : Exception::AccessViolationOperation::kRead;
#else
      access_violation_operation = (uint64_t(mcontext.gregs[REG_ERR]) & kX86PageFaultErrorCodeWrite)
                                       ? Exception::AccessViolationOperation::kWrite
                                       : Exception::AccessViolationOperation::kRead;
#endif  // REX_PLATFORM_PS5
#elif REX_ARCH_ARM64
      // For a Data Abort (EC - ESR_EL1 bits 31:26 - 0b100100 from a lower
      // Exception Level, 0b100101 without a change in the Exception Level),
      // bit 6 is 0 for reading from a memory location, 1 for writing to a
      // memory location.
      if (mcontext_esr && ((mcontext_esr->esr >> 26) & 0b111110) == 0b100100) {
        access_violation_operation = (mcontext_esr->esr & (UINT64_C(1) << 6))
                                         ? Exception::AccessViolationOperation::kWrite
                                         : Exception::AccessViolationOperation::kRead;
      } else {
        // Determine the memory access direction based on which instruction has
        // requested it.
        // esr_context may be unavailable on certain hosts (for instance, on
        // Android, it was added only in NDK r16 - which is the first NDK
        // version to support the Android API level 27, while NDK r15 doesn't
        // have esr_context in its API 26 sigcontext.h).
        // On AArch64 (unlike on AArch32), the program counter is the address of
        // the currently executing instruction.
        bool instruction_is_store;
        if (IsArm64LoadPrefetchStore(*reinterpret_cast<const uint32_t*>(mcontext.pc),
                                     instruction_is_store)) {
          access_violation_operation = instruction_is_store
                                           ? Exception::AccessViolationOperation::kWrite
                                           : Exception::AccessViolationOperation::kRead;
        } else {
          assert_always(
              "No ESR in the exception thread context, or it's not a Data "
              "Abort, and the faulting instruction is not a known load, "
              "prefetch or store instruction");
          access_violation_operation = Exception::AccessViolationOperation::kUnknown;
        }
      }
#else
      access_violation_operation = Exception::AccessViolationOperation::kUnknown;
#endif  // REX_ARCH
      ex.InitializeAccessViolation(&thread_context,
                                   reinterpret_cast<uint64_t>(signal_info->si_addr),
                                   access_violation_operation);
    } break;
    default:
      assert_unhandled_case(signal_number);
  }

  for (size_t i = 0; i < rex::countof(handlers_) && handlers_[i].first; ++i) {
    if (handlers_[i].first(&ex, handlers_[i].second)) {
      // Exception handled.
#if REX_ARCH_AMD64
#if REX_PLATFORM_PS5
      mcontext.mc_rip = __register_t(thread_context.rip);
      mcontext.mc_rflags = __register_t(thread_context.eflags);
      uint32_t modified_register_index;
      // The order must match the order in X64Register.
      __register_t* const kIntRegisterMap[] = {
          &mcontext.mc_rax, &mcontext.mc_rcx, &mcontext.mc_rdx, &mcontext.mc_rbx,
          &mcontext.mc_rsp, &mcontext.mc_rbp, &mcontext.mc_rsi, &mcontext.mc_rdi,
          &mcontext.mc_r8,  &mcontext.mc_r9,  &mcontext.mc_r10, &mcontext.mc_r11,
          &mcontext.mc_r12, &mcontext.mc_r13, &mcontext.mc_r14, &mcontext.mc_r15,
      };
      uint16_t modified_int_registers_remaining = ex.modified_int_registers();
      while (rex::bit_scan_forward(modified_int_registers_remaining, &modified_register_index)) {
        modified_int_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        *kIntRegisterMap[modified_register_index] =
            __register_t(thread_context.int_registers[modified_register_index]);
      }
      vec128_t* const xmm_registers = Ps5XmmRegisters(mcontext);
      uint16_t modified_xmm_registers_remaining = ex.modified_xmm_registers();
      while (rex::bit_scan_forward(modified_xmm_registers_remaining, &modified_register_index)) {
        modified_xmm_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        std::memcpy(&xmm_registers[modified_register_index],
                    &thread_context.xmm_registers[modified_register_index], sizeof(vec128_t));
      }
#else
      mcontext.gregs[REG_RIP] = greg_t(thread_context.rip);
      mcontext.gregs[REG_EFL] = greg_t(thread_context.eflags);
      uint32_t modified_register_index;
      // The order must match the order in X64Register.
      static const size_t kIntRegisterMap[] = {
          REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
          REG_R8,  REG_R9,  REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
      };
      uint16_t modified_int_registers_remaining = ex.modified_int_registers();
      while (rex::bit_scan_forward(modified_int_registers_remaining, &modified_register_index)) {
        modified_int_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        mcontext.gregs[kIntRegisterMap[modified_register_index]] =
            thread_context.int_registers[modified_register_index];
      }
      uint16_t modified_xmm_registers_remaining = ex.modified_xmm_registers();
      while (rex::bit_scan_forward(modified_xmm_registers_remaining, &modified_register_index)) {
        modified_xmm_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        std::memcpy(&mcontext.fpregs->_xmm[modified_register_index],
                    &thread_context.xmm_registers[modified_register_index], sizeof(vec128_t));
      }
#endif  // REX_PLATFORM_PS5
#elif REX_ARCH_ARM64
      uint32_t modified_register_index;
      uint32_t modified_x_registers_remaining = ex.modified_x_registers();
      while (rex::bit_scan_forward(modified_x_registers_remaining, &modified_register_index)) {
        modified_x_registers_remaining &= ~(UINT32_C(1) << modified_register_index);
        mcontext.regs[modified_register_index] = thread_context.x[modified_register_index];
      }
      mcontext.sp = thread_context.sp;
      mcontext.pc = thread_context.pc;
      mcontext.pstate = thread_context.pstate;
      if (mcontext_fpsimd) {
        mcontext_fpsimd->fpsr = thread_context.fpsr;
        mcontext_fpsimd->fpcr = thread_context.fpcr;
        uint32_t modified_v_registers_remaining = ex.modified_v_registers();
        while (rex::bit_scan_forward(modified_v_registers_remaining, &modified_register_index)) {
          modified_v_registers_remaining &= ~(UINT32_C(1) << modified_register_index);
          std::memcpy(&mcontext_fpsimd->vregs[modified_register_index],
                      &thread_context.v[modified_register_index], sizeof(vec128_t));
          mcontext.regs[modified_register_index] = thread_context.x[modified_register_index];
        }
      }
#endif  // REX_ARCH
      return;
    }
  }
#if REX_PLATFORM_PS5
  ForwardUnhandledException(signal_number, signal_info, signal_context);
#endif
}

void ExceptionHandler::Install(Handler fn, void* data) {
  if (!signal_handlers_installed_) {
    struct sigaction signal_handler;

    std::memset(&signal_handler, 0, sizeof(signal_handler));
    signal_handler.sa_sigaction = ExceptionHandlerCallback;
    signal_handler.sa_flags = SA_SIGINFO;

    if (sigaction(SIGILL, &signal_handler, &original_sigill_handler_) != 0) {
      assert_always("Failed to install new SIGILL handler");
    }
    if (sigaction(SIGSEGV, &signal_handler, &original_sigsegv_handler_) != 0) {
      assert_always("Failed to install new SIGSEGV handler");
    }
#if REX_PLATFORM_PS5
    MakeCodeReadable();
#endif
    signal_handlers_installed_ = true;
  }

  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (!handlers_[i].first) {
      handlers_[i].first = fn;
      handlers_[i].second = data;
      return;
    }
  }
  assert_always("Too many exception handlers installed");
}

void ExceptionHandler::Uninstall(Handler fn, void* data) {
  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (handlers_[i].first == fn && handlers_[i].second == data) {
      for (; i < rex::countof(handlers_) - 1; ++i) {
        handlers_[i] = handlers_[i + 1];
      }
      handlers_[i].first = nullptr;
      handlers_[i].second = nullptr;
      break;
    }
  }

  bool has_any = false;
  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (handlers_[i].first) {
      has_any = true;
      break;
    }
  }
  if (!has_any) {
    if (signal_handlers_installed_) {
      if (sigaction(SIGILL, &original_sigill_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGILL handler");
      }
      if (sigaction(SIGSEGV, &original_sigsegv_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGSEGV handler");
      }
      signal_handlers_installed_ = false;
    }
  }
}

}  // namespace rex::arch

#endif  // REX_PLATFORM_LINUX || REX_PLATFORM_MAC || REX_PLATFORM_PS5
