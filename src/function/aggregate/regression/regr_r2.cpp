// Returns the coefficient of determination for non-null pairs in a group.
// It is computed for non-null pairs using the following formula:
// null                 if var_pop(x) = 0, else
// 1                    if var_pop(y) = 0 and var_pop(x) <> 0, else
// power(corr(y,x), 2)

#include "duckdb/function/aggregate/algebraic/corr.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/function/aggregate/regression_functions.hpp"

namespace duckdb {
struct RegrR2State {
	CorrState corr;
	StddevState var_pop_x;
	StddevState var_pop_y;
};

struct RegrR2Operation {
	template <class STATE>
	static void Initialize(STATE *state) {
		CorrOperation::Initialize<CorrState>(&state->corr);
		STDDevBaseOperation::Initialize<StddevState>(&state->var_pop_x);
		STDDevBaseOperation::Initialize<StddevState>(&state->var_pop_y);
	}

	template <class A_TYPE, class B_TYPE, class STATE, class OP>
	static void Operation(STATE *state, FunctionData *bind_data, A_TYPE *x_data, B_TYPE *y_data, nullmask_t &anullmask,
	                      nullmask_t &bnullmask, idx_t xidx, idx_t yidx) {
		CorrOperation::Operation<A_TYPE, B_TYPE, CorrState, OP>(&state->corr, bind_data, y_data, x_data, bnullmask,
		                                                           anullmask, yidx, xidx);
		STDDevBaseOperation::Operation<A_TYPE, StddevState, OP>(&state->var_pop_x, bind_data, y_data, bnullmask,
		                                                           yidx);
		STDDevBaseOperation::Operation<A_TYPE, StddevState, OP>(&state->var_pop_y, bind_data, x_data, anullmask,
		                                                           xidx);
	}

	template <class STATE, class OP>
	static void Combine(STATE source, STATE *target) {
		CorrOperation::Combine<CorrState, OP>(source.corr, &target->corr);
		STDDevBaseOperation::Combine<StddevState, OP>(source.var_pop_x, &target->var_pop_x);
		STDDevBaseOperation::Combine<StddevState, OP>(source.var_pop_y, &target->var_pop_y);
	}

	template <class T, class STATE>
	static void Finalize(Vector &result, FunctionData *fd, STATE *state, T *target, nullmask_t &nullmask, idx_t idx) {
		auto var_pop_x = state->var_pop_x.count > 1 ? (state->var_pop_x.dsquared / state->var_pop_x.count) : 0;
		if (!Value::DoubleIsValid(var_pop_x)) {
			throw OutOfRangeException("VARPOP(X) is out of range!");
		}
		if (var_pop_x == 0) {
			nullmask[idx] = true;
			return;
		}
		auto var_pop_y = state->var_pop_y.count > 1 ? (state->var_pop_y.dsquared / state->var_pop_y.count) : 0;
		if (!Value::DoubleIsValid(var_pop_y)) {
			throw OutOfRangeException("VARPOP(Y) is out of range!");
		}
		if (var_pop_y == 0) {
			target[idx] = 1;
			return;
		}
		CorrOperation::Finalize<T, CorrState>(result, fd, &state->corr, target, nullmask, idx);
		target[idx] = pow(target[idx], 2);
	}

	static bool IgnoreNull() {
		return true;
	}
};

void RegrR2Fun::RegisterFunction(BuiltinFunctions &set) {
	AggregateFunctionSet fun("regr_r2");
	fun.AddFunction(AggregateFunction::BinaryAggregate<RegrR2State, double, double, double, RegrR2Operation>(
	    LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE));
	set.AddFunction(fun);
}
} // namespace duckdb
