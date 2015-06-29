/*
 * This file is part of MULTEM.
 * Copyright 2015 Ivan Lobato <Ivanlh20@gmail.com>
 *
 * MULTEM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MULTEM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MULTEM. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SPECIMEN_H
#define SPECIMEN_H

#include <random>

#include "math.cuh"
#include "types.hpp"
#include "atomic_data.hpp"
#include "atom_data.hpp"
#include "input_multislice.hpp"
#include "host_device_functions.cuh"

namespace multem
{
	template<class T, eDevice dev>
	class Specimen{
		public:
			using value_type_r = typename T;
			using size_type = std::size_t;

			Specimen():input_multislice(nullptr) {}

			void set_input_data(Input_Multislice<T, dev> *input_multislice_io)
			{
				input_multislice = input_multislice_io;

				/***************************************************************************/
				Atomic_Data atomic_data;
				atomic_data.Load_Data(input_multislice->potential_type);

				atom_type.resize(c_nAtomsTypes); 
				for(auto i=0; i<atom_type.size(); i++)
				{
					atomic_data.To_atom_type_CPU(i+1, input_multislice->Vrl, input_multislice->nR, input_multislice->grid.dR_min(), atom_type[i]);
				}

				/***************************************************************************/
				atoms_u.set_Atoms(input_multislice->atoms, input_multislice->grid.pbc_xy , &atom_type);
				atoms_u.Sort_by_z();
				atoms_u.get_z_layer();

				/***************************************************************************/
				if(atoms_u.s_z_Int < 2.0*input_multislice->grid.dz)
				{
					input_multislice->grid.dz = atoms_u.s_z_Int;
					input_multislice->interaction_model = eESIM_Phase_Object;
				}

				atoms.set_Atoms(atoms_u, false , &atom_type);

				/***************************************************************************/
				get_Slicing(input_multislice->interaction_model, input_multislice->potential_slicing, input_multislice->grid.dz);

				/***************************************************************************/
				rand.set_input_data(input_multislice->fp_seed, input_multislice->fp_dim);
			}

			/* Move atoms (ramdom distribution will be included in the future) */
			void move_atoms(const int &iconf)
			{
				if(!input_multislice->is_frozen_phonon()||(iconf <= 0))
				{
					return;
				}

				// set configuration
				rand.set_configuration(iconf);

				// Move atoms
				for(int iAtoms = 0; iAtoms<atoms_u.size(); iAtoms++)
				{
					atoms.Z[iAtoms] = atoms_u.Z[iAtoms];
					atoms.x[iAtoms] = atoms_u.x[iAtoms] + rand.dx(atoms_u.sigma[iAtoms]);
					atoms.y[iAtoms] = atoms_u.y[iAtoms] + rand.dy(atoms_u.sigma[iAtoms]);
					atoms.z[iAtoms] = atoms_u.z[iAtoms] + rand.dz(atoms_u.sigma[iAtoms]);
					atoms.sigma[iAtoms] = atoms_u.sigma[iAtoms];
					atoms.occ[iAtoms] = atoms_u.occ[iAtoms];
				}
				// get atom information
				atoms.get_Statistic(&atom_type);

				if(input_multislice->is_multislice())
				{
					// Ascending sort by z
					atoms.Sort_by_z();
					// Slicing procedure
					get_Slicing(input_multislice->interaction_model, input_multislice->potential_slicing, input_multislice->grid.dz);
				}
			}

			T get_dz(const int &islice)
			{
				return slice.dz(islice)/cos(input_multislice->theta);
			}

			//int IsInThickness(int islice);

			Input_Multislice<T, dev> *input_multislice; 			

			Vector<Atom_Type<T, e_Host>, e_Host> atom_type;		// Atom types
			Atom_Data<T> atoms; 								// displaced atoms
			Slice<T, e_Host> slice; 							// Slicing procedure
			Thickness<T, e_Host> thickness; 					// Thicknesses

		private:
			void get_z_Slice(const ePotential_Slicing &potential_slicing, T dz_i=0)
			{
				z_Slice.clear();

				auto quot = [&](const T &A, const T &B)->int
				{
					return (int)floor(A/B+Epsilon<T>::rel);
				};

				auto get_Spacing = [](size_type ix, const Vector<float, e_Host> &x)->float
				{
					ix = (ix <= 0)?1:min(ix, x.size()-1);
					return (x.size()>1)?x[ix]-x[ix-1]:0.0;
				};

				auto get_dz_b = [](const T &z_0, const T &z_e, const T &dz)->T
				{
					T dz_b = fmod(abs(z_0-z_e), dz);
					dz_b += (dz_b<((dz>2.0)?0.25:0.50)*dz)?dz:0.0;
					return dz_b;
				};

				switch(potential_slicing)
				{
					case ePS_Planes:
					{
						T dz_Bot = get_Spacing(0, atoms_u.z_layer);
						T dz_Top = get_Spacing(atoms_u.z_layer.size() - 1, atoms_u.z_layer);
						float layer_0 = atoms_u.z_layer.front() - 0.5*dz_Bot;
						float layer_e = atoms_u.z_layer.back() + 0.5*dz_Top;
						int nz_Bot = (atoms.z_min<layer_0)?quot(layer_0-atoms.z_min, dz_Bot) + 1:0;
						int nz_Top = (atoms.z_max>layer_e)?quot(atoms.z_max-layer_e, dz_Top) + 1:0;
						z_Slice.resize(atoms_u.z_layer.size()+nz_Bot+nz_Top+1);
						int j = 0;
						for(auto i=0; i <= nz_Bot; i++)
						{
							z_Slice[j++] = layer_0-(nz_Bot-i)*dz_Bot;
						}
						for(auto i=1; i<atoms_u.z_layer.size(); i++)
						{
							T dz = get_Spacing(i, atoms_u.z_layer);
							z_Slice[j++] = atoms_u.z_layer[i-1] + 0.5*dz;
						}
						for(auto i=0; i <= nz_Top; i++)
						{
							z_Slice[j++] = layer_e+i*dz_Bot;
						}
					}
					break;
					case ePS_dz_Proj:
					{
						/*******************************************************************/
						int nz = quot(atoms.s_z, dz_i )+ 1;
						z_Slice.resize(nz + 1);
						/*******************************************************************/
						z_Slice[0] = atoms.z_min-0.5*(nz*dz_i-atoms.s_z);
						for(auto i=1; i<z_Slice.size(); i++)
						{
							z_Slice[i] = z_Slice[i-1] + dz_i;
						}
					}
					break;
					case ePS_dz_Sub:
					{
						/*******************************************************************/
						int nz = quot(atoms_u.s_z, dz_i) + 1;
						T dz_Bot = get_dz_b(atoms.z_Int_min, atoms_u.z_min-0.5*(nz*dz_i-atoms_u.s_z), dz_i);
						T dz_Top = get_dz_b(atoms.z_Int_max, atoms_u.z_max+0.5*(nz*dz_i-atoms_u.s_z), dz_i);

						z_Slice.resize(quot(atoms.s_z_Int-dz_Bot-dz_Top, dz_i) + 3);
						/*******************************************************************/
						z_Slice[0] = atoms.z_Int_min;
						for(auto i=1; i<z_Slice.size(); i++)
						{
							T dz = (i == 1)?dz_Bot:(i == z_Slice.size()-1)?dz_Top:dz_i;
							z_Slice[i] = z_Slice[i-1] + dz;
						}
					}
					break;
				}
				z_Slice.shrink_to_fit();
			}

			// Slicing
			void get_Slicing(const eElec_Spec_Int_Model &interaction_model, const ePotential_Slicing &potential_slicing, T dz_i)
			{
				if(interaction_model != eESIM_Multislice) {
					slice.resize(1);
					slice.z_0[0] = atoms.z_Int_min; 
					slice.z_e[0] = atoms.z_Int_max; 
					slice.z_int_0[0] = slice.z_0[0];
					slice.z_int_e[0] = slice.z_e[0];
					slice.iatom_0[0] = 0;
					slice.iatom_e[0] = atoms.size()-1;

					thickness.resize(1);
					thickness.islice[0] = slice.size()-1;
					thickness.iatom_e[0] = slice.iatom_e[slice.size()-1];
					thickness.z[0] = atoms.z_max;
					thickness.z_zero_def[0] = input_multislice->zero_defocus_plane;
					thickness.z_back_prop[0] = thickness.z_zero_def[0] - atoms_u.z_max;

					return;
				}

				get_z_Slice(potential_slicing, dz_i);
				slice.resize(z_Slice.size()-1);

				for(auto islice=0; islice<slice.size(); islice++)
				{
					slice.z_0[islice] = z_Slice[islice]; 
					slice.z_e[islice] = z_Slice[islice + 1];
					switch(potential_slicing)
					{
						case ePS_Planes:
						{
							slice.z_int_0[islice] = slice.z_0[islice];
							slice.z_int_e[islice] = slice.z_e[islice];

							atoms.find_by_z(slice.z_int_0[islice], slice.z_int_e[islice], slice.iatom_0[islice], slice.iatom_e[islice], false);
						}
						break;
						case ePS_dz_Proj:
						{
							slice.z_int_0[islice] = slice.z_0[islice] ;
							slice.z_int_e[islice] = slice.z_e[islice] ;

							atoms.find_by_z(slice.z_int_0[islice], slice.z_int_e[islice], slice.iatom_0[islice], slice.iatom_e[islice], false);
						}
						break;
						case ePS_dz_Sub:
						{
							T zm = 0.5*(slice.z_0[islice]+ slice.z_e[islice]);

							slice.z_int_0[islice] = min(zm - atoms.R_Int_max, slice.z_0[islice]);
							slice.z_int_e[islice] = max(zm + atoms.R_Int_max, slice.z_e[islice]);

							atoms.find_by_z(slice.z_int_0[islice], slice.z_int_e[islice], slice.iatom_0[islice], slice.iatom_e[islice]);
						}
						break;
					}
				}

				if((slice.size() == 1)||(input_multislice->thickness_type == eTT_Whole_Specimen))
				{
					thickness.resize(1);
	
					thickness.islice[0] = slice.size()-1;
					thickness.iatom_e[0] = slice.iatom_e[slice.size()-1];
					thickness.z[0] = atoms.z_max;
					thickness.z_zero_def[0] = input_multislice->zero_defocus_plane;
					thickness.z_back_prop[0] = thickness.z_zero_def[0] - slice.z_e[slice.size()-1];
				}
			}

			// get thickness
			//void get_Thickness(eThickness_Type thickness_type);

			struct Random
			{
				public:
					void set_input_data(int &fp_seed_i, FP_Dim &fp_dim_i)
					{
						fp_seed = fp_seed_i;
						fp_dim = fp_dim_i;
					}

					void set_configuration(int iconf)
					{	
						gen_u.seed(fp_seed);
						rand_u.reset();
						unsigned int seed_x, seed_y, seed_z;
						for(auto i=0; i<iconf; i++)
						{
							seed_x = rand_u(gen_u);
							seed_y = rand_u(gen_u);
							seed_z = rand_u(gen_u);
						}
						gen_x.seed(seed_x);
						randn_x.reset();

						gen_y.seed(seed_y);
						randn_y.reset();

						gen_z.seed(seed_z);
						randn_z.reset();
					}

					T dx(const T &sigma)
					{
						return (fp_dim.x)?sigma*randn_x(gen_x):0.0;
					}

					T dy(const T &sigma)
					{
						return (fp_dim.y)?sigma*randn_y(gen_y):0.0;
					}

					T dz(const T &sigma)
					{
						return (fp_dim.z)?sigma*randn_z(gen_z):0.0;
					}
				private:
					int fp_seed;
					FP_Dim fp_dim;
					std::mt19937_64 gen_u;
					std::mt19937_64 gen_x;
					std::mt19937_64 gen_y;
					std::mt19937_64 gen_z;
					std::uniform_int_distribution<int> rand_u;
					std::normal_distribution<T> randn_x;
					std::normal_distribution<T> randn_y;
					std::normal_distribution<T> randn_z;
					//std::uniform_int_distribution<int> rand_u(0);
					//std::normal_distribution<T> randn_x(0.0, 1.0);
					//std::normal_distribution<T> randn_y(0.0, 1.0);
					//std::normal_distribution<T> randn_z(0.0, 1.0);
			};

			Random rand;
			// Undisplaced atoms
			Atom_Data<T> atoms_u;
			Vector<T, e_Host> z_Slice;
	};

	//// get thickness
	//template<class T>
	//void Specimen<T>::get_Thickness(eThickness_Type thickness_type)
	//{
	//	auto get_z_BackProp = [](eZero_Defocus_Type zero_defocus_type, T zero_defocus_plane, T z_min, T z_max)
	//	{
	//		switch(zero_defocus_type)
	//		{
	//			case eZDT_First:
	//			{
	//				zero_defocus_plane = z_min;
	//			}
	//			break;
	//			case eZDT_Middle:
	//			{
	//				zero_defocus_plane = 0.5*(z_min + z_max);
	//			}
	//			break;
	//			case eZDT_Last:
	//			{
	//				zero_defocus_plane = z_max;
	//			}
	//			break;
	//		}
	//
	//		return zero_defocus_plane - (atoms.z[thickness.iAtom[iThickness]]+atoms.R_Int_max);
	//	}
	//
	//	if((slice.size() == 1)||(thickness_type == eTT_Whole_Specimen))
	//	{
	//		thickness.resize(1);
	//
	//		thickness[0].islice = slice.size()-1;
	//		thickness[0].iatom_e = slice.back().iatom_e;
	//		thickness[0].z = atoms.z_max;
	//		thickness[0].z_BackProp = input_multislice->zero_defocus_plane - slice[nslice-1].ze;
	//		return;
	//	}
	//	
	//	int nz_i, nz;
	//	T *z_i = 0, *z = 0;
	//
	//	if(input_multislice->thickness_type == 3)
	//	{ 
	//		multem::MatchTwoVectors(input_multislice->nThickness_i, input_multislice->Thickness_i, nPlanes_u, Planes_u, nz_i, z_i);
	//	}
	//	else
	//	{
	//		nz_i = input_multislice->nThickness_i;
	//		z_i = new T [nz_i];
	//		memcpy(z_i, input_multislice->Thickness_i, nz_i*cSizeofRD); 
	//	}
	//
	//	nz = nslice;
	//	z = new T [nz];
	//	for(int iz=0; iz<nz; iz++) 
	//		z[iz] = 0.5*(slice[iz].z0+slice[iz].ze);
	//
	//	multem::MatchTwoVectors(nz_i, z_i, nz, z, thickness.n, thickness.h, thickness.islice);
	//	nz = 0; delete [] z; z = 0;
	//	nz_i = 0; delete [] z_i; z_i = 0;
	//
	//	delete [] thickness.zb; thickness.zb = new T[thickness.n];
	//	delete [] thickness.iAtom; thickness.iAtom = new int[thickness.n];
	//	for(int iThickness=0; iThickness<thickness.n; iThickness++)
	//	{
	//		thickness.iAtom[iThickness] = atoms.BinarySearch_by_z(thickness.h[iThickness], 0, atoms.n-1, eST_Top);
	//		thickness.zb[iThickness] = input_multislice->zero_defocus_plane - (atoms.z[thickness.iAtom[iThickness]]+atoms.R_Int_max);
	//	}
	//}
	//
	//template<class T>
	//int Specimen<T>::IsInThickness(int islice)
	//{
	//	int iThickness = -1;
	//	for(int i=0; i<thickness.n; i++)
	//	{
	//		if(thickness.islice[i] == islice)
	//		{
	//			iThickness = i;
	//			break;
	//		}
	//	}
	//
	//	return iThickness;
	//}
} // namespace multem

#endif