/*++
Copyright (c) 2021 Microsoft Corporation

Module Name:

    fixplex_def.h

Abstract:

    Fixed-precision unsigned integer simplex tableau.

Author:

    Nikolaj Bjorner (nbjorner) 2021-03-19
    Jakob Rath 2021-04-6

--*/

#pragma once

#include "math/polysat/fixplex.h"
#include "math/simplex/sparse_matrix_def.h"

namespace polysat {

    template<typename Ext>
    fixplex<Ext>::~fixplex() {
        reset();
    }    
    
    template<typename Ext>
    void fixplex<Ext>::ensure_var(var_t v) {
        while (v >= m_vars.size()) {
            M.ensure_var(m_vars.size());
            m_vars.push_back(var_info());            
        }
        if (m_to_patch.get_bounds() <= v) 
            m_to_patch.set_bounds(2*v+1);
    }

    template<typename Ext>
    void fixplex<Ext>::reset() {
        M.reset();
        m_to_patch.reset();
        m_vars.reset();
        m_rows.reset();
        m_left_basis.reset();
        m_base_vars.reset();

        // pivot(0,1, 2);
    }

    template<typename Ext>
    lbool fixplex<Ext>::make_feasible() {
        ++m_stats.m_num_checks;
        m_left_basis.reset();
        m_infeasible_var = null_var;
        unsigned num_iterations = 0;
        unsigned num_repeated = 0;
        var_t v = null_var;
        m_bland = false;
        SASSERT(well_formed());
        while ((v = select_var_to_fix()) != null_var) {
            TRACE("simplex", display(tout << "v" << v << "\n"););
            if (!m_limit.inc() || num_iterations > m_max_iterations) {
                return l_undef;
            }
            check_blands_rule(v, num_repeated);
            switch (make_var_feasible(v)) {
            case l_true:
                ++num_iterations;
                break;
            case l_false:
                m_to_patch.insert(v);
                m_infeasible_var = v;
                ++m_stats.m_num_infeasible;
                return l_false;
            case l_undef:
                m_to_patch.insert(v);
                return l_undef;
            }
        }
        SASSERT(well_formed());
        return l_true;
    }

    template<typename Ext>
    typename fixplex<Ext>::row 
    fixplex<Ext>::add_row(var_t base_var, unsigned num_vars, var_t const* vars, numeral const* coeffs) {
        m_base_vars.reset();
        row r = M.mk_row();
        for (unsigned i = 0; i < num_vars; ++i) 
            if (coeffs[i] != 0)                 
                M.add_var(r, coeffs[i], vars[i]);

        numeral base_coeff = 0;
        numeral value = 0;
        for (auto const& e : M.row_entries(r)) {
            var_t v = e.m_var;
            if (v == base_var) 
                base_coeff = e.m_coeff;
            else {
                if (is_base(v))
                    m_base_vars.push_back(v);
                value += e.m_coeff * m_vars[v].m_value;
            }
        }
        SASSERT(base_coeff != 0);
        SASSERT(!is_base(base_var));
        while (m_rows.size() <= r.id()) 
            m_rows.push_back(row_info());
        m_rows[r.id()].m_base = base_var;
        m_rows[r.id()].m_base_coeff = base_coeff;
        m_rows[r.id()].m_value = value;
        m_vars[base_var].m_base2row = r.id();
        m_vars[base_var].m_is_base = true;
        m_vars[base_var].m_value = 0 - (value / base_coeff);
        // TBD: record when base_coeff does not divide value
        add_patch(base_var);
        if (!m_base_vars.empty()) {
            gauss_jordan();
        }
        SASSERT(well_formed_row(r));
        SASSERT(well_formed());
        return r;
    }


    /**
     * increment v by delta
     */
    template<typename Ext>
    void fixplex<Ext>::update_value(var_t v, numeral const& delta) {
        if (delta == 0)
            return;
        m_vars[v].m_value += delta;
        SASSERT(!is_base(v));

        //
        // v <- v + delta
        // s*s_coeff + R = 0, where R contains v*v_coeff 
        // -> 
        // R.value += delta*v_coeff
        // s.value = - R.value / s_coeff
        // 
        for (auto c : M.col_entries(v)) {
            row r = c.get_row();
            row_info& ri = m_rows[r.id()];
            var_t s = ri.m_base;
            ri.m_value += delta * c.get_row_entry().m_coeff;
            m_vars[s].m_value = 0 - (ri.m_value / ri.m_base_coeff);
            add_patch(s);
        }            
    }    

    template<typename Ext>
    void fixplex<Ext>::gauss_jordan() {
        while (!m_base_vars.empty()) {
            auto v = m_base_vars.back();
            auto rid = m_vars[v].m_base2row;
#if 0
            auto const& row = m_rows[rid];
            make_basic(v, row);
#endif
        }
    }

    /**
     * If v is already a basic variable in preferred_row, skip
     * If v is non-basic but basic in a different row, then 
     *      eliminate v from one of the rows.
     * If v if non-basic
     */

    template<typename Ext>
    void fixplex<Ext>::make_basic(var_t v, row const& preferred_row) {

        NOT_IMPLEMENTED_YET();
        
    }

    template<typename Ext>
    bool fixplex<Ext>::in_bounds(var_t v) const {
        return in_bounds(value(v), lo(v), hi(v));
    }

    template<typename Ext>
    bool fixplex<Ext>::in_bounds(numeral const& val, numeral const& lo, numeral const& hi) const {
        if (lo == hi)
            return true;
        if (lo < hi)
            return lo <= val && val < hi;
        return val < hi || lo <= val;
    }


    /**
     * Attempt to improve assigment to make x feasible.
     * 
     * return l_false if x is base variable of infeasible row
     * return l_true if it is possible to find an assignment that improves
     * return l_undef if the row could not be used for an improvement
     * 
     * delta - improvement over previous gap to feasible bound.
     * new_value - the new value to assign to x within its bounds.
     */

    template<typename Ext>
    lbool fixplex<Ext>::make_var_feasible(var_t x) {
        if (in_bounds(x))
            return l_true;
        auto val = value(x);
        numeral new_value, b, delta;
        if (lo(x) - val < val - hi(x)) {
            new_value = lo(x); 
            delta = new_value - val;
        }
        else {
            new_value = hi(x) - 1; 
            delta = val - new_value;
        }
 
        var_t y = select_pivot_core(x, delta, new_value, b);

        if (y == null_var) {
            if (is_infeasible_row(x))
                return l_false;
            else
                return l_undef;
        }
        
        pivot(x, y, b, new_value);
        return l_true;
    }

    /**
       \brief Select a variable y in the row r defining the base var x, 
       s.t. y can be used to patch the error in x_i.  Return null_var
       if there is no y. Otherwise, return y and store its coefficient
       in out_b.

       The routine gives up if the coefficients of all free variables do not have the minimal
       number of trailing zeros. 
    */
    template<typename Ext>
    typename fixplex<Ext>::var_t 
    fixplex<Ext>::select_pivot_core(var_t x, numeral const& delta, 
                                    numeral const& new_value, numeral & out_b) {
        SASSERT(is_base(x));
        var_t max    = get_num_vars();
        var_t result = max;
        row r = row(m_vars[x].m_base2row);
        int n = 0;
        unsigned best_col_sz = UINT_MAX;
        int best_so_far    = INT_MAX;
        numeral row_value = m_rows[r.id()].m_value;
        numeral delta_y;
        bool best_in_bounds = false;

        for (auto const& r : M.row_entries(r)) {
            var_t y = r.m_var;          
            numeral const & b = r.m_coeff;  
            if (x == y) 
                continue;
            if (!has_minimal_trailing_zeros(y, b))
                continue;
            numeral new_y_value = (row_value - b*value(y) - a*new_value)/b;
            bool _in_bounds = in_bounds(new_y_value, lo(y), hi(y));
            if (!_in_bounds) {
                if (lo(y) - new_y_value < new_y_value - hi(y))
                    delta_y = new_y_value - lo(y);
                else
                    delta_y = hi(y) - new_y_value;
            }
            int num  = get_num_non_free_dep_vars(y, best_so_far);
            unsigned col_sz = M.column_size(y);
            bool is_improvement = false, is_plateau = false;

            // improvement criteria would need some scrutiny.
            if (best_so_far == INT_MAX)
                is_improvement = true;
            else if (!best_in_bounds && _in_bounds)
                is_improvement = true;
            else if (!best_in_bounds && !_in_bounds && delta_y < delta_best)
                is_improvement = true;
            else if (best_in_bounds && _in_bounds && num < best_so_far)
                is_improvement = true;
            else if (best_in_bounds && _in_bounds && num == best_so_far && col_sz < best_col_sz)
                is_improvement = true;
            else if (best_in_bounds && delta_y == delta_best && num_best_so_far && col_sz == best_col_sz)
                is_plateau = true;
            
            if (is_improvement) {
                result       = y;
                out_b        = b;
                best_so_far  = num;
                best_col_sz  = col_sz;
                best_in_bounds = _in_bounds;
                n            = 1;
            } 
            else if (is_pleateau) {
                n++;
                if (m_random() % n == 0) {
                    result   = y;
                    out_b    = b;
                }
            }                              
        }
        return result < max ? result : null_var;
    }

    template<typename Ext>
    bool fixplex<Ext>::has_minimal_trailing_zeros(var_t y, numeral const& b) {
        unsigned tz1 = trailing_zeros(b);
        if (tz1 == 0)
            return true;
        for (auto col : M.col_entries(y)) {
            numeral c = col.get_row_entry().m_coeff;
            unsigned tz2 = trailing_zeros(c);
            if (tz1 > tz2)
                return false;
        }
        return true;
    }


    template<typename Ext>
    bool fixplex<Ext>::is_infeasible_row(var_t x) {
        SASSERT(is_base(x));
        auto& row = m_rows[m_vars[x].m_base2row];
        numeral lo_sum = 0, hi_sum = 0, diff = 0;
        for (auto const& e : M.row_entries(r)) {
            var_t v = e.m_var;
            numeral const& c = e.m_coeff;
            if (lo(v) == hi(v))
                return false;
            lo_sum += lo(v) * c;
            hi_sum += (hi(v) - 1) * c;
            numeral range = hi(v) - lo(v);
            if (!m.signed_mul(range, c, range))
                return false;
            if (!m.signed_add(diff, diff, range))
                return false;
        }        
        return 0 < lo_sum && lo_sum <= hi_sum;
    }


    /**
       \brief Given row

         r_x = a*x + b*y + rest = 0

       Assume:

         base(r_x) = x
         value(r_x) = value(b*y + rest)
         old_value(y) := value(y)

       Effect:

         base(r_x)  := y
         value(x)   := new_value            
         value(r_x) := value(r_x) - b*value(y) + a*new_value
         value(y)   := -value(r_x) / b
         base_coeff(r_x) := b
 
       Let r be a row where y has coefficient c != 0.
       Assume: trailing_zeros(c) >= trailing_zeros(b)
       
         z = base(r)
         d = base_coeff(r)
         b1 = (b >> tz(b))
         c1 = (c >> (tz(c) - tz(b)))       
         r <- b1 * r  - c1 * r_x
         value(r) := b1 * value(r) - b1 * old_value(y) - c1 * value(r_x)
         value(z) := - value(r) / d
         base_coeff(r) := b1 * base_coeff(r)
    */    
    template<typename Ext>
    void fixplex<Ext>::pivot(var_t x, var_t y, numeral const& b, numeral const& new_value) {
        ++m_stats.m_num_pivots;
        SASSERT(is_base(x));
        SASSERT(!is_base(y));
        var_info& xI = m_vars[x];
        var_info& yI = m_vars[y];
        unsigned rx = xI.m_base2row;
        auto& row_x = m_rows[rx];
        numeral const& a = row_x.m_base_coeff;
        numeral old_value_y = yI.m_value;
        row_x.m_base = y;
        row_x.m_value = row_i.m_value - b*old_value_y + a*new_value;
        row_x.m_base_coeff = b;
        yI.m_base2row = rx;
        yI.m_is_base = true;
        yI.m_value = 0 - row_x.m_value / b;
        xI.m_is_base = false;
        xI.m_value = new_value;
        row r_x(rx);
        add_patch(y);
        SASSERT(well_formed_row(r_x));

        unsigned tz1 = trailing_zeros(b);
 
        for (auto col : M.col_entries(y)) {
            row r_z = col.get_row();
            unsigned rz = r_z.id();
            if (rz == rx)
                continue;
            auto& row_z = m_rows[rz];
            var_info& zI = m_vars[row_z];
            numeral c = col.get_row_entry().m_coeff;
            unsigned tz2 = trailing_zeros(c);
            SASSERT(tz1 <= tz2);
            numeral b1 = b >> tz1;
            numeral c1 = m.inv(c >> (tz2 - tz1));
            M.mul(r_z, b1);
            M.add(r_z, c1, r_x);
            row_z.m_value = (b1 * (row_z.m_value - old_value_y)) + c1 * row_x.m_value;
            row_z.m_base_coeff *= b1;
            zI.m_value = 0 - row_z.m_value / row_z.m_base_coeff;
            SASSERT(well_formed_row(r_z));
            add_patch(zI.m_base);
        }
        SASSERT(well_formed());
    }


}
