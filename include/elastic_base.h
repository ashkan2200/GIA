/* TODO

 */
#ifndef ELASTIC_BASE_H
#define ELASTIC_BASE_H

#include <deal.II/base/convergence_table.h>
#include <deal.II/base/function.h>
#include <deal.II/base/logstream.h>
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/timer.h>
#include <deal.II/base/utilities.h>
#include <deal.II/dofs/block_info.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_boundary_lib.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/lac/block_sparse_matrix.h>
#include <deal.II/lac/block_vector.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/sparse_direct.h>
#include <deal.II/lac/sparse_ilu.h>
#include <deal.II/lac/trilinos_block_sparse_matrix.h>
#include <deal.II/lac/trilinos_block_vector.h>
#include <deal.II/lac/trilinos_precondition.h>
#include <deal.II/lac/trilinos_sparse_matrix.h>
#include <deal.II/lac/trilinos_vector.h>
#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/data_out_faces.h>
#include <deal.II/numerics/error_estimator.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/vector_tools.h>

#include <fstream>
#include <sstream>
#include <string>
#include <typeinfo>

#include "boundary.h"
#include "coefficient.h"
#include "exact.h"
#include "parameters.h"
#include "rhs.h"
#include "SurfaceDataOut.h"

using namespace dealii;
namespace Elastic
{

template <int dim>
class ElasticBase {
public:
    ElasticBase (const unsigned int degree, const int _info, const int _n_blocks);
    void run ();

protected:
    // pointer to parameter object
    parameters *par;
    // conditional outputs
    ostream & oout;
    // timer
    TimerOutput timer;

    const unsigned int						degree;
    const unsigned int						n_blocks, n_components;
    Triangulation<dim>						triangulation;

    FESystem<dim>							fe;
    DoFHandler<dim>							dof_handler;

    ConstraintMatrix                        constraints;

    BlockSparsityPattern					sparsity_pattern;
    TrilinosWrappers::BlockSparseMatrix		system_matrix;
    TrilinosWrappers::BlockSparseMatrix		system_preconditioner; 		// preconditioner [A 0;Bt S]

    TrilinosWrappers::BlockVector			solution;
    TrilinosWrappers::BlockVector			system_rhs, load, body_force, precond_rhs;

    /*!
     * Purly virtual methods.
     * Need to be implemented in the child class
     */
    virtual void setup_AMG () = 0;
    virtual void solve () = 0;

    void create_geometry();
    void setup_dofs ();
    void assemble_system ();

    std::string to_upper(const std::string str);
    void generate_matlab_study();
    // Write matrix to data file
    void write_matrix(const FullMatrix<double> &M, string filename );
    // Write matrix to data file
    void write_matrix(TrilinosWrappers::SparseMatrix &M, string filename );
    // Write vector to data file
    void write_vector(const TrilinosWrappers::BlockVector &V, string filename );
    // Computer the error
    void compute_errors (double &u_l2_error, double &p_l2_error) const;
    void output_results ();
    void output_surface ();

private:
    std::vector<unsigned int> dofs_per_component;
    std::vector<unsigned int> dofs_per_block;
};
}

// ------------- IMPLEMENTATION --------------

template <int dim>
Elastic::ElasticBase<dim>::ElasticBase (const unsigned int degree, const int _info, const int _n_blocks)
    :
      oout(std::cout),
      timer (oout,
             TimerOutput::summary,
             TimerOutput::wall_times),
      degree (degree),
      n_blocks(_n_blocks),
      n_components(dim+1),
      triangulation (Triangulation<dim>::maximum_smoothing),
      fe (FE_Q<dim>(degree+1), dim,
          FE_Q<dim>(degree), 1),
      dof_handler (triangulation),
      dofs_per_component(std::vector<unsigned int>(n_components)),
      dofs_per_block(std::vector<unsigned int>(n_blocks))
{
    par = parameters::getInstance();
}

template <int dim>
void
Elastic::ElasticBase<dim>::create_geometry(){
    // Number of initial subdivisions for each axis
    std::vector<unsigned int> subdivisions (dim, 1);
    subdivisions[0] = par->xdivisions;
    subdivisions[1] = par->ydivisions;

    const Point<dim> bottom_left = (dim == 2 ?
                                        Point<dim>(par->x1,par->y1) :
                                        Point<dim>(par->x1,0,par->y1));

    const Point<dim> top_right   = (dim == 2 ?
                                        Point<dim>(par->x2,par->y2) :
                                        Point<dim>(par->x2,1,par->y2));

    // Creating the grid
    GridGenerator::subdivided_hyper_rectangle (triangulation,
                                               subdivisions,
                                               bottom_left,
                                               top_right);

    // Refine the mesh with the number of refinement in the parameters.
    triangulation.refine_global (par->refinements);


    // Set boundary flags
    for (typename Triangulation<dim>::active_cell_iterator
         cell = triangulation.begin_active();
         cell != triangulation.end(); ++cell)
        for (unsigned int f=0; f < GeometryInfo<dim>::faces_per_cell; ++f){
            if (cell->face(f)->at_boundary()){

                const Point<dim> face_center = cell->face(f)->center();

                // If x component of the face's center is on the left boundary,
                // the face is one the left boundary
                if (face_center[dim-2] == par->x1)
                    cell->face(f)->set_boundary_indicator(par->b_left);
                // If x component of the face's center is on the right boundary,
                // the face is on the right boundary.
                else if (face_center[dim-2] == par->x2)
                    cell->face(f)->set_boundary_indicator(par->b_right);
                // If y component of the face's center is on the bottom boundary,
                // the face is on the bottom boundary.
                else if (face_center[dim-1] == par->y1)
                    cell->face(f)->set_boundary_indicator(par->b_bottom);
                /** If y component of the face's center is on the top boundary
                 * and the boundary is under the ice, it is flagged as b_ice
                 * otherwise it is b_up
                 **/
                else if (face_center[dim-1] == par->y2){
                    if(face_center[0] <= par->Ix)
                        cell->face(f)->set_boundary_indicator(par->b_ice);
                    else
                        cell->face(f)->set_boundary_indicator(par->b_up);
                }
            }// at boundary
        }// for faces
}

template <int dim>
void
Elastic::ElasticBase<dim>::setup_dofs (){

    dof_handler.distribute_dofs (fe);

    // Renumber to reduce sparsity band
    DoFRenumbering::Cuthill_McKee (dof_handler);

    // Renumber component wise
    std::vector<unsigned int> block_component (n_components,0);
    for(int i=0; i<n_components; ++i)
        block_component[i] = i;

    // DOF renumbering
    DoFRenumbering::component_wise (dof_handler, block_component);

    //Interpolate boudaries using constraint matrix
    {
        std::vector<bool> ns_mask (dim+1, true); // NO_SLIP
        std::vector<bool> vs_mask (dim+1, true); // V_SLIP

        ns_mask[0] = true;
        ns_mask[1] = true;
        ns_mask[2] = false;

        vs_mask[0] = true;
        vs_mask[1] = false;
        vs_mask[2] = false;

        constraints.clear();
        VectorTools::interpolate_boundary_values (dof_handler,
                                                  bFlags::NO_SLIP,
                                                  ZeroFunction<dim>(n_components),
                                                  constraints,
                                                  ns_mask);

        VectorTools::interpolate_boundary_values (dof_handler,
                                                  bFlags::V_SLIP,
                                                  ZeroFunction<dim>(n_components),
                                                  constraints,
                                                  vs_mask);
    }
    constraints.close();

    system_matrix.clear ();
    system_preconditioner.clear ();

    // Count the number of DOFs per block
    DoFTools::count_dofs_per_block (dof_handler, dofs_per_component, block_component);

    if(n_blocks == 2){
        for(int i=0; i<n_components-1; ++i)
            dofs_per_block[0] += dofs_per_component[i];
        dofs_per_block[1] = dofs_per_component[n_components-1];
    }else{
        for(int i=0; i<n_components; ++i)
            dofs_per_block[i] = dofs_per_component[i];
    }

    // Create sparsity pattern
    {
        BlockCompressedSimpleSparsityPattern bcsp(n_blocks, n_blocks);

        for(int i=0; i<n_blocks; ++i)
            for(int j=0; j<n_blocks; ++j)
                bcsp.block(i,j).reinit (dofs_per_block[i],
                                        dofs_per_block[j]);


        bcsp.collect_sizes();
        DoFTools::make_sparsity_pattern (dof_handler, bcsp, constraints, true);

        sparsity_pattern.copy_from(bcsp);
    }

    system_matrix.reinit (sparsity_pattern);
    system_preconditioner.reinit (sparsity_pattern);

    solution.reinit (n_blocks);
    system_rhs.reinit (n_blocks);
    precond_rhs.reinit (n_blocks);
    load.reinit (n_blocks);
    body_force.reinit (n_blocks);

    for(int i=0; i<n_blocks; ++i){
        solution.block(i).reinit (dofs_per_block[i]);
        system_rhs.block(i).reinit (dofs_per_block[i]);
        precond_rhs.block(i).reinit (dofs_per_block[i]);
        load.block(i).reinit (dofs_per_block[i]);
        body_force.block(i).reinit (dofs_per_block[i]);
    }

    solution.collect_sizes ();
    system_rhs.collect_sizes ();
    precond_rhs.collect_sizes ();
    load.collect_sizes ();
    body_force.collect_sizes ();
}

template <int dim>
void
Elastic::ElasticBase<dim>::assemble_system ()
{
    system_matrix=0;
    system_rhs=0;

    QGauss<dim>   quadrature_formula(degree+2);
    QGauss<dim-1> face_quadrature_formula(degree+2);

    FEValues<dim> fe_values (fe, quadrature_formula,
                             update_values    |
                             update_quadrature_points  |
                             update_JxW_values |
                             update_gradients);

    FEFaceValues<dim> fe_face_values (fe, face_quadrature_formula,
                                      update_values    | update_normal_vectors |
                                      update_quadrature_points  | update_JxW_values);

    const unsigned int   dofs_per_cell   = fe.dofs_per_cell;

    const unsigned int   n_q_points      = quadrature_formula.size();
    const unsigned int   n_face_q_points = face_quadrature_formula.size();

    // Block information for reordering and preconditioner computing.
    dof_handler.initialize_local_block_info();
    BlockInfo bf = dof_handler.block_info();
    BlockIndices bi = bf.local();

    // deal.ii local structure
    int *local_dim = new int[n_components];
    int *local_dim_start = new int[n_components];

    unsigned int dim_u = 0,		// dim * Q2 nodes
            dim_p = 0;// 1 * Q1 nodes

    // for components
    for(int i = 0; i< n_components-1; i++){
        local_dim[i]        = bi.block_size(i); // Size of the blocks in local matrix
        local_dim_start[i]  = bi.block_start(i); // Starting index of the blocks in the reordered local matrix
        dim_u += bi.block_size(i);
    }
    //for pressure
    local_dim[n_components-1]        = bi.block_size(n_components-1); // Size of the blocks in local matrix
    local_dim_start[n_components-1]  = bi.block_start(n_components-1); // Starting index of the blocks in the reordered local matrix
    dim_p = bi.block_size(n_components-1);

    int            *l_u = new int[dim_u];
    int            *l_p = new int[dim_p];
    int            *order = new int[dofs_per_cell];

    for(int i = 0; i < dofs_per_cell; i++){
        unsigned int ir = bf.renumber(i);
        order[ir]=i;
    }

    for(int i = 0; i < dim_u; i++)
        l_u[i] = order[i];

    for(int i = 0; i < dim_p; i++)
        l_p[i] = order[i+dim_u];

    FullMatrix<double>	cell_matrix  (dofs_per_cell, dofs_per_cell),
            cell_ordered (dofs_per_cell, dofs_per_cell),
            cell_precond (dofs_per_cell, dofs_per_cell),
            l_A          (dim_u,dim_u),
            l_Bt         (dim_u,dim_p),
            l_B          (dim_p,dim_u),
            l_C          (dim_p,dim_p),
            l_S          (dim_p,dim_p),
            l_Ainv       (dim_u,dim_u),
            l_Adiag      (dim_u,dim_u); // laplacian

    // Dummy cell matrix for preconditioner, it is allways zero
    Vector<double>      cell_rhs (dofs_per_cell),
            cell_pre_rhs(dofs_per_cell);

    std::vector<unsigned int> local_dof_indices (dofs_per_cell);

    const RightHandSide<dim>			right_hand_side;
    const BoundaryValues<dim>			boundaries;
    std::vector<Vector<double> >		rhs_values (n_q_points, Vector<double>(dim+1));
    std::vector<Vector<double> >		boundary_values (n_face_q_points, Vector<double>(dim+1));

    Coefficients<dim> 				  	 coeff(par->YOUNG,par->POISSON);
    std::vector<double>     		  	 mu_values (n_q_points);
    std::vector<double>     		  	 beta_values (n_q_points);	// mu^2/alpha

    const FEValuesExtractors::Vector displacements (0);
    const FEValuesExtractors::Scalar pressure (dim);

    /*! @todo check if this works correctly for 3d as well.
     */
    // Set everything to zero
    // Change the last component to one
    Tensor<1,dim> e(0);
    e[dim-1] = 1.0;

    std::vector<SymmetricTensor<2,dim> > symgrad_phi_u	(dofs_per_cell);
    std::vector<Tensor<2,dim> >          grad_phi		(dofs_per_cell);
    std::vector<Tensor<1,dim> >			 phi_u			(dofs_per_cell);
    std::vector<double>                  div_phi_u		(dofs_per_cell);
    std::vector<double>                  phi_p			(dofs_per_cell);

    bool first = true;
    unsigned int counter = 0;
    double h;

    typename DoFHandler<dim>::active_cell_iterator
            cell = dof_handler.begin_active(),
            endc = dof_handler.end();
    for (; cell!=endc; ++cell)
    {
        fe_values.reinit (cell);
        cell_matrix		= 0;
        cell_rhs		= 0;
        cell_pre_rhs    = 0;
        cell_precond	= 0;
        l_Adiag			= 0;

        right_hand_side.vector_value_list(fe_values.get_quadrature_points(),
                                          rhs_values);

        coeff.mu_value_list     (fe_values.get_quadrature_points(), mu_values);
        coeff.beta_value_list   (fe_values.get_quadrature_points(), beta_values);

        for (unsigned int q=0; q<n_q_points; ++q)
        {
            for (unsigned int k=0; k < dofs_per_cell; ++k)
            {
                symgrad_phi_u[k] = fe_values[displacements].symmetric_gradient (k, q);
                grad_phi[k]		 = fe_values[displacements].gradient (k, q);
                phi_u[k]		 = fe_values[displacements].value (k, q);
                div_phi_u[k]     = fe_values[displacements].divergence (k, q);
                phi_p[k]         = fe_values[pressure].value (k, q);
            }

            for (unsigned int i=0; i<dofs_per_cell; ++i)
            {
                const unsigned int component_i =
                        fe.system_to_component_index(i).first;

                for (unsigned int j=0; j < dofs_per_cell; ++j)
                {
                    const unsigned int component_j =
                            fe.system_to_component_index(j).first;

                    cell_matrix(i,j) += (
                                symgrad_phi_u[i] * symgrad_phi_u[j] * 2 * mu_values[q]         // A
                                - grad_phi[j]  * e * phi_u[i] * par->scale3 * par->adv_enabled	// A-adv
                                + div_phi_u[j] * e * phi_u[i] * par->scale3 * par->div_enabled	// A-div
                                + div_phi_u[i] * phi_p[j] * mu_values[q]                       // Bt
                                + phi_p[i] * div_phi_u[j] * mu_values[q]                       // B
                                - phi_p[i] * phi_p[j] * beta_values[q]                         // C
                                )* fe_values.JxW(q);

                    cell_precond(i,j) += (
                                phi_p[i] * div_phi_u[j] * mu_values[q]				// B
                                )* fe_values.JxW(q);

                }// end j

                cell_rhs(i) +=  phi_u[i] * e * par->weight * fe_values.JxW(q); // load vector, body force
            }// end i
        } // end q

        // Neumann Boundary conditions (Ice-Load and free surface)
        for (unsigned int face_num=0; face_num<GeometryInfo<dim>::faces_per_cell; ++face_num){
            if (cell->face(face_num)->at_boundary()
                    && (cell->face(face_num)->boundary_indicator() == par->b_ice ) ){
                // Update face values
                fe_face_values.reinit (cell, face_num);

                // Update boundary values
                boundaries.vector_value_list(fe_face_values.get_quadrature_points(),
                                             boundary_values);

                for (unsigned int q=0; q<n_face_q_points; ++q)
                    for (unsigned int i=0; i<dofs_per_cell; ++i){
                        const unsigned int
                                component_i = fe.system_to_component_index(i).first;

                        cell_rhs(i) +=  fe_face_values.shape_value(i, q) *
                                boundary_values[q](component_i) *
                                fe_face_values.JxW(q);
                    }
            }// end if at boundary
        }// end face

        // Local assemble and Schur generation
        // extract here using velocities, pressure
        // This is the ordered matrices, i and j corespond to ordered values
        for (unsigned int i=0; i<dofs_per_cell; ++i){// shape index i
            for (unsigned int j=0; j < dofs_per_cell; ++j){// shape index j

                cell_ordered(i,j) = cell_matrix(order[i],order[j]); // local matrix ordered by u0,u1...un,v0,v1...vn,p0,p1...pm

                if(i < dim_u && j < dim_u){
                    l_A(i,j)  = cell_ordered(i,j);
                    l_Ainv(i,j) = l_A(i,j);
                }else if(i < dim_u && j >= dim_u){
                    l_Bt(i,j-dim_u) = cell_ordered(i,j);
                }else if(i >= dim_u && j < dim_u){
                    l_B(i-dim_u,j)  = cell_ordered(i,j);
                }else if(i >= dim_u && j >= dim_u){
                    l_C(i-dim_u,j-dim_u)  = cell_ordered(i,j);
                    l_S(i-dim_u,j-dim_u)  = l_C(i-dim_u,j-dim_u); // -C ... look at the sign
                }
            }
        }
        // boundary conditions

        // Local Schur calculation	l_A(k,k) += h*h;// A(i,j) + h²I
        h = cell->diameter();
        l_Ainv.diagadd(h*h);
        l_Ainv.gauss_jordan(); // Compute A inverse

        l_S.triple_product 	(	l_Ainv,l_B,l_Bt,false,false, -1.0  );
        // End Schur calculation

        // begin Schur assembly preconditioner
        for (unsigned int i=0; i< dim_p; ++i)// shape index i
            for (unsigned int j=0; j < dim_p; ++j)
                cell_precond(l_p[i],l_p[j]) = l_S(i,j);

        // end Schur assembly preconditioner

        // begin assembly A preconditioner
        for (unsigned int i=0; i< dim_u; ++i)// shape index i
            for (unsigned int j=0; j < dim_u; ++j)
                cell_precond(l_u[i],l_u[j]) = l_A(i,j);

        // end assembly A preconditioner

        // printing local matrices
        if(par->print_matrices && first){
            write_matrix(cell_matrix,"l_m");
            write_matrix(cell_precond,"l_p");
        }

        // local-to-global
        cell->get_dof_indices (local_dof_indices);

        constraints.distribute_local_to_global(cell_matrix, cell_rhs,
                                               local_dof_indices,
                                               system_matrix, system_rhs);
        constraints.distribute_local_to_global(cell_precond, cell_pre_rhs,
                                               local_dof_indices,
                                               system_preconditioner, precond_rhs);
        // end local-to-global

        first = false;
        counter++;
    } // end cell
    free(l_u);
    free(l_p);
    free(order);
}

template <int dim>
void
Elastic::ElasticBase<dim>::compute_errors (double &u_l2_error, double &p_l2_error) const
{
    const ComponentSelectFunction<dim>
            pressure_mask (dim, dim+1);
    const ComponentSelectFunction<dim>
            velocity_mask(std::make_pair(0, dim), dim+1);

    ExactSolution<dim> exact_solution;
    Vector<double> cellwise_errors (triangulation.n_active_cells());


    QIterated<dim> quadrature ( QTrapez<1>(), degree+1);

    // L2-norm
    VectorTools::integrate_difference (dof_handler, solution, exact_solution,
                                       cellwise_errors, quadrature,
                                       VectorTools::L2_norm,
                                       &pressure_mask);

    p_l2_error = cellwise_errors.l2_norm();

    VectorTools::integrate_difference (dof_handler, solution, exact_solution,
                                       cellwise_errors, quadrature,
                                       VectorTools::L2_norm,
                                       &velocity_mask);

    u_l2_error = cellwise_errors.l2_norm();
    // end L2-norm
}

template <int dim>
void
Elastic::ElasticBase<dim>::run ()
{
    int inv_iter = 0, schur_iter = 0;
#ifdef LOG_RUN
    oout << GREEN << "Logging enabled." << RESET << std::endl;
#else
    oout << GREEN << "Logging disabled." << RESET << std::endl;
#endif

    // Printing application variable
    par->print_variables(oout);

    create_geometry();

    timer.enter_section("DOF setup");
    setup_dofs ();
    timer.exit_section("DOF setup");

    /// Terminal output
    oout << "Active cells: "
         << triangulation.n_active_cells() << std::endl;

    oout << "Degrees of freedom: "
         << dof_handler.n_dofs() << " (";
    std::copy(dofs_per_component.begin(),
              dofs_per_component.end(),
              std::ostream_iterator<int>(oout,"+") );
    oout << "\b)" << std::endl;

    oout << GREEN << "\tAssembling" << RESET << flush;
    timer.enter_section("Assembling");
    assemble_system ();
    timer.exit_section();

    oout << GREEN << " | Setup AMG" << RESET << flush;
    timer.enter_section("Setup AMG");
    setup_AMG ();
    timer.exit_section();

    oout << GREEN << " | Solve system" << RESET << flush;
    timer.enter_section("System solver");
    solve ();
    timer.exit_section();

    if(par->output_results){
        oout << GREEN << " | Extract results" << RESET << flush;
        output_results ();

        oout << GREEN << " | Extract surface" << RESET << flush;
        output_surface();
    }

    oout << std::endl;

    if(par->print_matrices ){
        oout << RED << "\t\t>>> Printing matrices in Matlab format <<<" << RESET << std::endl;
        generate_matlab_study();
    }
    // printing solver info
    for(unsigned int i = 0; i < par->inv_iterations.size(); i++){
        inv_iter   += par->inv_iterations[i];
        schur_iter += par->schur_iterations[i];
    }

    // average inner iterations
    inv_iter /= par->inv_iterations.size();
    schur_iter /=par->schur_iterations.size();

    if(par->x2 == par->Ix ){
        double u_er = 0, p_er = 0;
        compute_errors (u_er, p_er);

        oout << "Errors: ||e_u||_L2, ||e_p||_L2 = "
             << u_er << "," << p_er << std::endl;
    }

    oout   << "FGMRES iterations: system(P_00,Schur) = "
           << par->system_iter
           << "(" << inv_iter << ", " << schur_iter << ")"
           << std::endl;
}

template <int dim>
void
Elastic::ElasticBase<dim>::output_results ()
{
    using namespace std;
    vector<string> solution_names (dim, "displacements");
    solution_names.push_back ("pressure");

    vector<DataComponentInterpretation::DataComponentInterpretation>
            data_component_interpretation(dim,
                                          DataComponentInterpretation::component_is_part_of_vector);
    data_component_interpretation
            .push_back (DataComponentInterpretation::component_is_scalar);

    DataOut<dim> data_out;
    data_out.attach_dof_handler (dof_handler);
    data_out.add_data_vector (solution, solution_names,
                              DataOut<dim>::type_dof_data,
                              data_component_interpretation);
    data_out.build_patches ();

    ostringstream filename;
    filename << "solution_" << par->POISSON	<< ".vtu";

    ofstream output (filename.str().c_str());
    data_out.write_vtu (output);
}

/*!
 * Output surface values to file.
 */
template <int dim>
void
Elastic::ElasticBase<dim>::output_surface() {
    using namespace std;
    vector<string> solution_names (dim, "displacements");
    solution_names.push_back ("pressure");

    vector<DataComponentInterpretation::DataComponentInterpretation>
            data_component_interpretation(dim,
                                          DataComponentInterpretation::component_is_part_of_vector);
    data_component_interpretation
            .push_back (DataComponentInterpretation::component_is_scalar);

    SurfaceDataOut<dim> data_out;
    data_out.attach_dof_handler (dof_handler);
    data_out.add_data_vector (solution, solution_names,
                              DataOutFaces<dim>::type_dof_data,
                              data_component_interpretation);
    data_out.build_patches ();

    ostringstream filename;
    filename << "surface_" << par->str_poisson	<< ".gnuplot";

    ofstream output (filename.str().c_str());
    data_out.write_gnuplot (output);
}

// Change string to uppercase
template <int dim>
std::string
Elastic::ElasticBase<dim>::to_upper(const std::string str){
    std::string out_str(str);
    for (int i = 0; i < str.size(); ++i)
        out_str[i] = toupper(str[i]);
    return out_str;
}

// Create matlab code
template <int dim>
void
Elastic::ElasticBase<dim>::generate_matlab_study(){
    ostringstream ss;
    ofstream myfile;
    // Extract node points
    std::vector<Point<dim> > support_points(dof_handler.n_dofs());
    const MappingQ<dim> mapping(degree);
    DoFTools::map_dofs_to_support_points(mapping,dof_handler,support_points);

    // Open file
    myfile.open("nodes.dat");
    if(!myfile.is_open()){
        cout << "Print_matlab: Unable to open file...";
        return;
    }

    for(unsigned int i=0; i<support_points.size(); ++i)
            myfile << support_points[i]<< std::endl;

    myfile.close();

    // Write all Aij blocks
    for(int i=0; i<n_blocks; ++i){
        for(int j=0; j<n_blocks; ++j){
            ss << "a" << i << j;
            write_matrix(system_matrix.block(i,j),ss.str());
            ss.str("");
        }
    }
    // Flush the stream
    ss.str("");
    // Write only the last Preconditioner part since this is the only different part from system matrix
    ss << "p" << n_blocks-1 << n_blocks-1;
    write_matrix(system_preconditioner.block(n_blocks-1,n_blocks-1),ss.str());
    ss.str("");

    write_vector(system_rhs,"rhs");

    // Open file
    ss << "matrices" << par->str_poisson << ".m";
    myfile.open(ss.str().c_str());
    ss.str("");

    if(!myfile.is_open()){
        cout << "Print_matlab: Unable to open file...";
        return;
    }

    myfile << "%loading data" << std::endl;

    for(int i=0; i<n_blocks; ++i){
        for(int j=0; j<n_blocks; ++j){
            ss << "a" << i << j
               << " = load('data_a" << i << j << ".dat');" << std::endl;

            ss << "a" << i << j << "(:,1:2) = "
               << "a" << i << j << "(:,1:2) + 1;" << std::endl;

            ss << "a" << i << j << " = spconvert("
               << "a" << i << j << ");" << std::endl;

            ss << "p" << i << j
               << " = a" << i << j << ";";
            myfile << ss.str() << endl;
            ss.str("");
        }
    }

    // Load Schur preconditioner to matlab
    ss << "p" << n_blocks-1 << n_blocks-1
       << " = load('data_p" << n_blocks-1 << n_blocks-1 << ".dat');" << std::endl;

    ss << "p" << n_blocks-1 << n_blocks-1 << "(:,1:2) = "
       << "p" << n_blocks-1 << n_blocks-1 << "(:,1:2) + 1;" << std::endl;

    ss << "p" << n_blocks-1 << n_blocks-1 << " = spconvert("
       << "p" << n_blocks-1 << n_blocks-1 << ");" << std::endl;
    myfile << ss.str() << endl;
    ss.str("");

    myfile << "load('data_l_m.dat');"  << endl
           << "load('data_l_p.dat');"  << endl;

    myfile << "% Creating A and P matrices" << std::endl
           << "A = [";
    for(int i=0; i<n_blocks; ++i){
        for(int j=0; j<n_blocks; ++j){
            ss << "a" << i << j << " ";
            myfile << ss.str();
            ss.str("");
        }
        myfile << ";" << endl;
    }
    myfile << "];" <<endl;


    myfile << "P = [";
    for(int i=0; i<n_blocks; ++i){
        for(int j=0; j<n_blocks; ++j){
            ss << "p"  << i << j << " ";
            myfile << ss.str();
            ss.str("");
        }
        myfile << ";" << endl;
    }
    myfile << "];";

    myfile.close();
}

// Create a matlab file with matrices
template <int dim>
void
Elastic::ElasticBase<dim>::write_matrix(TrilinosWrappers::SparseMatrix &M, string filename ){
    register double val = 0;
    register int row, col;
    string name = "data_" + filename + ".dat";
    FILE *fp;
    fp = fopen(name.c_str(),"w");


    TrilinosWrappers::SparseMatrix::iterator it = M.begin(),
            it_end = M.end();

    int i = 0;
    for (; it!=it_end; ++it) {
        val = it.operator*().value();
        if(val == 0)
            continue;
        row = it.operator*().row();
        col = it.operator*().column();
        fprintf(fp, "%d %d %.20f\n", row, col, val);
        ++i;
    }

    fclose(fp);
}

// Create a matlab file with vectors
template <int dim>
void
Elastic::ElasticBase<dim>::write_vector(const TrilinosWrappers::BlockVector &V, string filename ){
    int PSC = 13;
    //Printing in matlab form
    string name = "data_" + filename + ".dat";
    std::ofstream vecFile (name.c_str());
    vecFile << setprecision(PSC);
    V.print(vecFile);
    vecFile.close();
}

template <int dim>
void
Elastic::ElasticBase<dim>::write_matrix(const FullMatrix<double> &M, string filename ){
    //Printing in matlab form
    string name = "data_" + filename + ".dat";
    std::ofstream matFile (name.c_str());
    M.print(matFile,10);
    matFile.close();
}


#endif // ELASTIC_BASE_H
