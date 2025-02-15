#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <dmt/fdmt_cpu.hpp>

namespace py = pybind11;

// helper function to avoid making a copy when returning a py::array_t
// source: https://github.com/pybind/pybind11/issues/1042#issuecomment-642215028
template <typename Sequence>
inline py::array_t<typename Sequence::value_type> as_pyarray(Sequence&& seq) {
    auto size = seq.size();
    auto data = seq.data();
    std::unique_ptr<Sequence> seq_ptr =
        std::make_unique<Sequence>(std::forward<Sequence>(seq));
    auto capsule = py::capsule(seq_ptr.get(), [](void* p) {
        std::unique_ptr<Sequence>(reinterpret_cast<Sequence*>(p)); // NOLINT
    });
    seq_ptr.release();
    return py::array(size, data, capsule);
}

template <typename Sequence>
inline py::array_t<typename Sequence::value_type>
as_pyarray_ref(const Sequence& seq) {
    auto size        = seq.size();
    const auto* data = seq.data();
    return py::array_t<typename Sequence::value_type>(size, data);
}

PYBIND11_MODULE(libdmt, mod) {
    mod.doc() = "Python Bindings for dmt";
    py::class_<FDMTCoordMapping>(mod, "FDMTCoordMapping")
        .def_readonly("head", &FDMTCoordMapping::head)
        .def_readonly("tail", &FDMTCoordMapping::tail)
        .def_readonly("offset", &FDMTCoordMapping::offset);

    py::class_<FDMTPlan>(mod, "FDMTPlan")
        .def_readonly("df_top", &FDMTPlan::df_top)
        .def_readonly("df_bot", &FDMTPlan::df_bot)
        .def_readonly("state_shape", &FDMTPlan::state_shape)
        .def_readonly("coordinates", &FDMTPlan::coordinates)
        .def_readonly("coordinates_copy", &FDMTPlan::coordinates_to_copy)
        .def_readonly("mappings", &FDMTPlan::mappings)
        .def_readonly("mappings_copy", &FDMTPlan::mappings_to_copy)
        .def_readonly("state_sub_idx", &FDMTPlan::state_sub_idx)
        .def_readonly("dt_grid", &FDMTPlan::dt_grid)
        .def_property_readonly(
            "dt_grid_sub_top",
            [](const FDMTPlan& plan) {
                py::list res_list;
                for (const auto& inner_vec : plan.dt_grid_sub_top) {
                    res_list.append(
                        as_pyarray(static_cast<DtGridType>(inner_vec)));
                }
                return res_list;
            })
        .def("calculate_memory_usage", &FDMTPlan::calculate_memory_usage);

    py::class_<FDMTCPU> cls_fdmt(mod, "FDMT");
    cls_fdmt.def(
        py::init<float, float, size_t, size_t, float, size_t, size_t, size_t>(),
        py::arg("f_min"), py::arg("f_max"), py::arg("nchans"),
        py::arg("nsamps"), py::arg("tsamp"), py::arg("dt_max"),
        py::arg("dt_step") = 1, py::arg("dt_min") = 0);
    cls_fdmt.def_property_readonly("df", &FDMTCPU::get_df);
    cls_fdmt.def_property_readonly("correction", &FDMTCPU::get_correction);
    cls_fdmt.def_property_readonly("niters", &FDMTCPU::get_niters);
    cls_fdmt.def_property_readonly("fdmt_plan", &FDMTCPU::get_plan);
    cls_fdmt.def_property_readonly("dt_grid_final", [](FDMTCPU& fdmt) {
        return as_pyarray_ref(fdmt.get_dt_grid_final());
    });
    cls_fdmt.def_property_readonly("dm_grid_final", [](FDMTCPU& fdmt) {
        return as_pyarray(fdmt.get_dm_grid_final());
    });
    cls_fdmt.def_static("set_log_level", &FDMTCPU::set_log_level,
                        py::arg("level"));
    cls_fdmt.def_static("set_num_threads", &FDMTCPU::set_num_threads,
                        py::arg("nthreads"));
    // execute take 2d array as input, and return 2d array as output
    cls_fdmt.def(
        "execute", [](FDMTCPU& fdmt,
                      const py::array_t<float, py::array::c_style>& waterfall) {
            const auto* shape = waterfall.shape();
            const auto dt_final_size =
                static_cast<ssize_t>(fdmt.get_dt_grid_final().size());
            py::array_t<float, py::array::c_style> dmt(
                {dt_final_size, shape[1]});
            fdmt.execute(waterfall.data(), waterfall.size(), dmt.mutable_data(),
                         dmt.size());
            return dmt;
        });
    cls_fdmt.def(
        "initialise",
        [](FDMTCPU& fdmt,
           const py::array_t<float, py::array::c_style>& waterfall) {
            const auto* shape = waterfall.shape();
            const auto& plan  = fdmt.get_plan();
            const auto nchans_ndt =
                static_cast<ssize_t>(plan.state_shape[0][3]);
            py::array_t<float, py::array::c_style> state(
                {nchans_ndt, shape[1]});
            std::fill(state.mutable_data(), state.mutable_data() + state.size(),
                      0.0F);
            fdmt.initialise(waterfall.data(), state.mutable_data());
            return state;
        });
}
