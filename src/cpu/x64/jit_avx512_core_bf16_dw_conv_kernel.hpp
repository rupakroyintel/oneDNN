/*******************************************************************************
* Copyright 2019-2025 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef CPU_X64_JIT_AVX512_CORE_BF16_DW_CONV_KERNEL_HPP
#define CPU_X64_JIT_AVX512_CORE_BF16_DW_CONV_KERNEL_HPP

#include <memory>
#include "common/c_types_map.hpp"
#include "common/memory_tracking.hpp"

#include "cpu/x64/injectors/jit_uni_postops_injector.hpp"
#include "cpu/x64/jit_generator.hpp"
#include "cpu/x64/jit_primitive_conf.hpp"

#include "cpu/x64/jit_avx512_core_bf16cvt.hpp"

namespace dnnl {
namespace impl {
namespace cpu {
namespace x64 {

struct jit_avx512_dw_conv_fwd_kernel_bf16 : public jit_generator_t {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_dw_conv_fwd_kernel_bf16)

    jit_avx512_dw_conv_fwd_kernel_bf16(
            const jit_conv_conf_t &ajcp, const memory_desc_t &dst_md);

    jit_conv_conf_t jcp;

private:
    using reg64_t = const Xbyak::Reg64;
    using mask_t = const Xbyak::Opmask;
    const Xbyak::AddressFrame &vmmword = zword;

    const int acc_idx_start = 2;
    inline int get_max_regs() const { return isa_has_bf16(jcp.isa) ? 30 : 25; };

    // dw convolution
    reg64_t reg_input = r8;
    reg64_t aux_reg_input = r9;
    reg64_t reg_kernel = r10;
    reg64_t aux_reg_kernel = r11;
    reg64_t reg_ch_blocks = r12;
    reg64_t reg_output = r13;
    reg64_t reg_bias = r14;
    reg64_t reg_kh = r15;
    reg64_t iter_kh = rax;
    reg64_t reg_oi = rbx;

    reg64_t reg_tmp = reg_ch_blocks;

    // fused convolution
    reg64_t reg_input_buffer_ptr = rdx;
    reg64_t aux_reg_input_buffer_ptr = rsi;
    reg64_t reg_iw_offset = reg_input; //Hack: clear reg_input early in kernel
    reg64_t reg_tail = rax;
    mask_t k_oc_tail_mask = Xbyak::Opmask(2);
    mask_t ktail_mask = k_oc_tail_mask;
    mask_t k_ch_tail_mask_extended = Xbyak::Opmask(3);

    Xbyak::Zmm zmm_ker_reg = Xbyak::Zmm(0);
    Xbyak::Zmm zmm_src_reg = Xbyak::Zmm(1);
    Xbyak::Zmm zmm_prev_dst = Xbyak::Zmm(31);

    /* Registers used for bfloat16 emulation */
    Xbyak::Zmm bf16_emu_reserv_1 = Xbyak::Zmm(26);
    Xbyak::Zmm bf16_emu_reserv_2 = Xbyak::Zmm(27);
    Xbyak::Zmm bf16_emu_reserv_3 = Xbyak::Zmm(28);
    reg64_t bf16_emu_reserv_4 = abi_not_param1;
    Xbyak::Zmm bf16_emu_reserv_5 = Xbyak::Zmm(29);
    Xbyak::Zmm bf16_emu_reserv_6 = Xbyak::Zmm(30);

    int get_acc_reg_idx(int idx) const;

    Xbyak::Zmm get_acc_reg(int idx);

    int get_ow_start(int ki, int pad_l) const {
        return nstl::max(0,
                utils::div_up(pad_l - ki * (jcp.dilate_w + 1), jcp.stride_w));
    }

    int get_ow_end(int ur_w, int ki, int pad_r) const {
        return ur_w
                - nstl::max(0,
                        utils::div_up(
                                pad_r - (jcp.kw - 1 - ki) * (jcp.dilate_w + 1),
                                jcp.stride_w));
    }

    inline bool is_src_layout_nxc() const {
        return utils::one_of(jcp.src_tag, format_tag::ndhwc, format_tag::nhwc,
                format_tag::nwc);
    }

    inline bool is_dst_layout_nxc() const {
        return utils::one_of(jcp.dst_tag, format_tag::ndhwc, format_tag::nhwc,
                format_tag::nwc);
    }

    inline void load_src(int ur_ch_blocks, int ur_w, bool last_ch_block_flag);
    inline void compute_loop(int ur_w, int ur_ch_blocks, int pad_l, int pad_r);
    inline void loop_ow(int ur_ch_blocks);
    inline void apply_filter_unrolled(int ur_ch_blocks, int ur_w, int pad_l,
            int pad_r, bool last_ch_block_flag);
    inline void apply_postops(
            int ur_ch_blocks, int ur_w, bool last_ch_block_flag);
    inline void store_dst(int ur_ch_blocks, int ur_w, bool last_ch_block_flag);

    std::unique_ptr<injector::jit_uni_postops_injector_t<avx512_core>>
            postops_injector_;

    std::unique_ptr<bf16_emulation_t> bf16_emu_;

    void generate() override;
};

struct jit_avx512_dw_conv_bwd_data_kernel_bf16 : public jit_generator_t {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_dw_conv_bwd_data_kernel_bf16)

    jit_avx512_dw_conv_bwd_data_kernel_bf16(const jit_conv_conf_t &ajcp)
        : jit_generator_t(jit_name()), jcp(ajcp), bf16_emu_(nullptr) {

        if (!isa_has_bf16(jcp.isa))
            bf16_emu_ = utils::make_unique<bf16_emulation_t>(this,
                    bf16_emu_reserv_1, bf16_emu_reserv_2, bf16_emu_reserv_3,
                    bf16_emu_reserv_4, bf16_emu_reserv_5, bf16_emu_reserv_6);
    }

    ~jit_avx512_dw_conv_bwd_data_kernel_bf16() override = default;

    jit_conv_conf_t jcp;

private:
    using reg64_t = const Xbyak::Reg64;

    const int acc_idx_start = 2;
    inline int get_max_regs() const { return isa_has_bf16(jcp.isa) ? 30 : 25; };

    Xbyak::Zmm zmm_ker_reg = Xbyak::Zmm(0);
    Xbyak::Zmm zmm_dst_reg = Xbyak::Zmm(1);

    inline Xbyak::Zmm get_acc_reg(int idx) {
        assert(idx + acc_idx_start <= get_max_regs());
        return Xbyak::Zmm(idx + acc_idx_start);
    }

    reg64_t reg_ddst = rax;
    reg64_t aux_reg_ddst = r8;
    reg64_t aux1_reg_ddst = abi_not_param1;
    reg64_t reg_kernel = rdx;
    reg64_t aux_reg_kernel = r10;
    reg64_t aux1_reg_kernel = rbp;
    reg64_t reg_dsrc = rsi;

    reg64_t reg_ur_str_w = r9;
    reg64_t reg_ch_blocks = rbx;

    reg64_t iter_kh = r11;
    reg64_t iter_kw = r12;
    reg64_t reg_kh = r13;
    reg64_t reg_kw = r14;

    reg64_t aux_reg_ch_blocks = r15;
    reg64_t reg_tmp = r15;
    Xbyak::Opmask k_ch_tail_mask = Xbyak::Opmask(1);

    Xbyak::Zmm bf16_emu_reserv_1 = Xbyak::Zmm(26);
    Xbyak::Zmm bf16_emu_reserv_2 = Xbyak::Zmm(27);
    Xbyak::Zmm bf16_emu_reserv_3 = Xbyak::Zmm(28);
    reg64_t bf16_emu_reserv_4 = iter_kw;
    Xbyak::Zmm bf16_emu_reserv_5 = Xbyak::Zmm(29);
    Xbyak::Zmm bf16_emu_reserv_6 = Xbyak::Zmm(30);

    std::unique_ptr<bf16_emulation_t> bf16_emu_;

    inline void ch_loop_body(int ur_ch_blocks, int unroll_w);
    inline void unroll_width_body(int ur_ch_blocks);
    inline void load_ddst(int ur_ch_blocks, int ur_str_w);
    inline void apply_filter(int ur_ch_blocks, int ur_str_w, bool is_last_ch);
    inline void store_dsrc(int ur_ch_blocks, int ur_str_w, bool is_last_ch);

    void generate() override;
    inline bool is_dsrc_layout_nxc() const {
        return utils::one_of(jcp.src_tag, format_tag::ndhwc, format_tag::nhwc,
                format_tag::nwc);
    }
    inline bool is_ddst_layout_nxc() const {
        return utils::one_of(jcp.dst_tag, format_tag::ndhwc, format_tag::nhwc,
                format_tag::nwc);
    }

    DNNL_DISALLOW_COPY_AND_ASSIGN(jit_avx512_dw_conv_bwd_data_kernel_bf16);
};

struct jit_avx512_dw_conv_bwd_weights_kernel_bf16 : public jit_generator_t {

    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_dw_conv_bwd_weights_kernel_bf16)

    jit_avx512_dw_conv_bwd_weights_kernel_bf16(const jit_conv_conf_t &ajcp)
        : jit_generator_t(jit_name()), jcp(ajcp), bf16_emu_(nullptr) {

        if (!isa_has_bf16(jcp.isa))
            bf16_emu_ = utils::make_unique<bf16_emulation_t>(this,
                    bf16_emu_reserv_1, bf16_emu_reserv_2, bf16_emu_reserv_3,
                    bf16_emu_reserv_4, bf16_emu_reserv_5, bf16_emu_reserv_6);
    }

    ~jit_avx512_dw_conv_bwd_weights_kernel_bf16() override = default;

    jit_conv_conf_t jcp;

private:
    using reg64_t = const Xbyak::Reg64;
    const Xbyak::AddressFrame &vmmword = zword;

    const int max_unroll_w_ = 30;
    const int block_size_ = 15;

    const int idx_start = 2;
    inline int get_max_regs() const { return isa_has_bf16(jcp.isa) ? 30 : 25; };

    /* Offset between input and accummulators is 3, therefore, assume 'kw'
     * is no larger than 3*/
    Xbyak::Zmm zmm_bias_reg = Xbyak::Zmm(0);
    Xbyak::Zmm zmm_out_reg = Xbyak::Zmm(1);

    inline Xbyak::Zmm get_acc_reg(int idx) {
        assert(idx + idx_start <= get_max_regs());
        return Xbyak::Zmm(idx + idx_start);
    }
    inline Xbyak::Zmm get_input_reg(int idx) {
        const int i_idx = idx_start + jcp.kw + idx % jcp.kw;
        assert(i_idx <= get_max_regs());
        return Xbyak::Zmm(i_idx);
    }

    reg64_t reg_tmp_input = r9;
    reg64_t reg_tmp_output = r10;
    reg64_t reg_tmp_filter = r13;
    reg64_t reg_kh_offset = rax;

    /* parameter passed by driver into kernel */
    Xbyak::Reg8 reg_exec_flags = bl;
    reg64_t reg_oh_worksize = r14;
    reg64_t reg_oh = rax;
    reg64_t reg_iter_ow_blk = r11;
    reg64_t reg_kh_aux = rsi;
    reg64_t reg_kh = rdx;

    /* Base addresses for convolution parameters. */
    reg64_t reg_input_baddr = r15;
    reg64_t reg_output_baddr = r12;
    reg64_t reg_filter_baddr = abi_not_param1;
    reg64_t reg_bias_baddr = r13;

    reg64_t reg_tmp = r8;

    Xbyak::Opmask k_ch_tail_mask = Xbyak::Opmask(1);

    /* Registers used for bfloat16 emulation */
    Xbyak::Zmm bf16_emu_reserv_1 = Xbyak::Zmm(26);
    Xbyak::Zmm bf16_emu_reserv_2 = Xbyak::Zmm(27);
    Xbyak::Zmm bf16_emu_reserv_3 = Xbyak::Zmm(28);
    reg64_t bf16_emu_reserv_4 = r8;
    Xbyak::Zmm bf16_emu_reserv_5 = Xbyak::Zmm(29);
    Xbyak::Zmm bf16_emu_reserv_6 = Xbyak::Zmm(30);

    std::unique_ptr<bf16_emulation_t> bf16_emu_;

    DNNL_DISALLOW_COPY_AND_ASSIGN(jit_avx512_dw_conv_bwd_weights_kernel_bf16)

    /* Micro-kernel JIT'ing, fusing 'kw' and 'ow_block' loops into unrolled FMAs
     */
    void compute_ow_step_unroll(int unroll_w, int l_pad, int pad_offset,
            int ow_block, bool is_last_ch);

    /* JIT'ing the outer loops for the micro-kernel -> {kh, oh_block} */
    void compute_kh_step(int unroll_w, int l_pad, int pad_offset, int ow_block,
            bool is_last_ch);
    /* Channel loop for 'nxc' format */
    void compute_ch_loop(int unroll_w, int l_pad, int pad_offset, int ow_block);
    void compute_h_loop(int unroll_w, int l_pad, int pad_offset, int ow_block);

    /* Write 'width' micro-kernel JITs; depending on the padding and convolution
     * size, write a micro-kernel for the left ow-block, middle ow-block(s), and
     * right ow-block.*/
    void compute_ow_block_unroll();
    void deploy_zero_filter();
    void zero_filter_kh_loop();
    void load_filter(bool is_last_ch = false);
    void zero_filter();
    void load_bias(bool is_last_ch);
    void zero_bias();
    void compute_bias_step_unroll(const int unroll_w, bool is_last_ch);
    void compute_ch_loop_bias(bool do_load_bias);
    void deploy_ch_loop_bias();
    void compute_single_ch_block_bias();
    void compute_spatial_loop_bias(bool is_last_ch);
    void store_filter(bool is_last_ch = false);
    void store_bias(bool is_last_ch);
    void compute_bias();
    void calculate_w_unrolling(
            int &unroll_trips, int &unroll_w, int &unroll_w_tail);

    void generate() override;

    inline bool is_layout_nxc() const {
        return utils::everyone_is(
                true, is_src_layout_nxc(), is_ddst_layout_nxc());
    }
    inline bool is_src_layout_nxc() const {
        return utils::one_of(jcp.src_tag, format_tag::ndhwc, format_tag::nhwc,
                format_tag::nwc);
    }
    inline bool is_ddst_layout_nxc() const {
        return utils::one_of(jcp.dst_tag, format_tag::ndhwc, format_tag::nhwc,
                format_tag::nwc);
    }
};

} // namespace x64
} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif // CPU_X64_JIT_AVX512_CORE_BF16_DW_CONV_KERNEL_HPP
