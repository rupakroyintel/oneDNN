/*******************************************************************************
* Copyright 2025 FUJITSU LIMITED
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

#ifndef CPU_AARCH64_JIT_BRGEMM_CONV_BWD_HPP
#define CPU_AARCH64_JIT_BRGEMM_CONV_BWD_HPP

#include "common/c_types_map.hpp"
#include "common/dnnl_thread.hpp"
#include "common/memory_tracking.hpp"
#include "common/primitive.hpp"
#include "common/utils.hpp"

#include "cpu/aarch64/jit_brgemm_conv.hpp"

namespace dnnl {
namespace impl {
namespace cpu {
namespace aarch64 {

template <cpu_isa_t isa>
struct brgemm_convolution_bwd_t : public primitive_t {

    struct pd_t : public cpu_convolution_bwd_data_pd_t {
        using cpu_convolution_bwd_data_pd_t::cpu_convolution_bwd_data_pd_t;

        DECLARE_COMMON_PD_T(name_.c_str(), brgemm_convolution_bwd_t);

        status_t init(engine_t *engine);

        std::shared_ptr<primitive_desc_t> fwd_pd_;

    private:
        std::string name_ = JIT_IMPL_NAME_HELPER("brg_conv_bwd:", isa, "");

        void init_name() {
            name_.append("+");
            name_.append(fwd_pd_->name());
        }
    };

    brgemm_convolution_bwd_t(const pd_t *apd) : primitive_t(apd) {};

    ~brgemm_convolution_bwd_t() override = default;

    status_t init(engine_t *engine) override;

    status_t execute(const exec_ctx_t &ctx) const override;

private:
    const pd_t *pd() const {
        return static_cast<const pd_t *>(primitive_t::pd().get());
    }
    std::shared_ptr<primitive_t> fwd_p_;
};

} // namespace aarch64
} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif

// vim: et ts=4 sw=4 cindent cino+=l0,\:4,N-s
