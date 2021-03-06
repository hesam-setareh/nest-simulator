/*
 *  rate_neuron_ipn_impl.h
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef RATE_NEURON_IPN_IMPL_H
#define RATE_NEURON_IPN_IMPL_H

#include "rate_neuron_ipn.h"

// C++ includes:
#include <cmath> // in case we need isnan() // fabs
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

// Includes from libnestutil:
#include "numerics.h"

// Includes from nestkernel:
#include "exceptions.h"
#include "kernel_manager.h"
#include "universal_data_logger_impl.h"

// Includes from sli:
#include "dict.h"
#include "dictutils.h"
#include "doubledatum.h"
#include "integerdatum.h"

namespace nest
{

/* ----------------------------------------------------------------
 * Recordables map
 * ---------------------------------------------------------------- */

template < class TNonlinearities >
RecordablesMap< rate_neuron_ipn< TNonlinearities > >
  rate_neuron_ipn< TNonlinearities >::recordablesMap_;

/* ----------------------------------------------------------------
 * Default constructors defining default parameters and state
 * ---------------------------------------------------------------- */

template < class TNonlinearities >
nest::rate_neuron_ipn< TNonlinearities >::Parameters_::Parameters_()
  : tau_( 10.0 )   // ms
  , lambda_( 1.0 ) // ms
  , std_( 1.0 )
  , mean_( 0.0 )
  , linear_summation_( true )
  , rectify_output_( false )
  , mult_coupling_( false )
{
  recordablesMap_.create();
}

template < class TNonlinearities >
nest::rate_neuron_ipn< TNonlinearities >::State_::State_()
  : rate_( 0.0 )
  , noise_( 0.0 )
{
}

/* ----------------------------------------------------------------
 * Parameter and state extractions and manipulation functions
 * ---------------------------------------------------------------- */

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::Parameters_::get(
  DictionaryDatum& d ) const
{
  def< double >( d, names::tau, tau_ );
  def< double >( d, names::lambda, lambda_ );
  def< double >( d, names::std, std_ );
  def< double >( d, names::mean, mean_ );
  def< bool >( d, names::linear_summation, linear_summation_ );
  def< bool >( d, names::rectify_output, rectify_output_ );
  def< bool >( d, names::mult_coupling, mult_coupling_ );
}

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::Parameters_::set(
  const DictionaryDatum& d )
{
  updateValue< double >( d, names::tau, tau_ );
  updateValue< double >( d, names::lambda, lambda_ );
  updateValue< double >( d, names::mean, mean_ );
  updateValue< double >( d, names::std, std_ );
  updateValue< bool >( d, names::linear_summation, linear_summation_ );
  updateValue< bool >( d, names::rectify_output, rectify_output_ );
  updateValue< bool >( d, names::mult_coupling, mult_coupling_ );

  if ( tau_ <= 0 )
    throw BadProperty( "Time constant must be > 0." );
  if ( lambda_ < 0 )
    throw BadProperty( "Passive decay rate must be >= 0." );
  if ( std_ < 0 )
    throw BadProperty( "Standard deviation of noise must not be negative." );
}

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::State_::get(
  DictionaryDatum& d ) const
{
  def< double >( d, names::rate, rate_ );   // Rate
  def< double >( d, names::noise, noise_ ); // Noise
}

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::State_::set(
  const DictionaryDatum& d )
{
  updateValue< double >( d, names::rate, rate_ ); // Rate
}

template < class TNonlinearities >
nest::rate_neuron_ipn< TNonlinearities >::Buffers_::Buffers_(
  rate_neuron_ipn< TNonlinearities >& n )
  : logger_( n )
{
}

template < class TNonlinearities >
nest::rate_neuron_ipn< TNonlinearities >::Buffers_::Buffers_( const Buffers_&,
  rate_neuron_ipn< TNonlinearities >& n )
  : logger_( n )
{
}

/* ----------------------------------------------------------------
 * Default and copy constructor for node
 * ---------------------------------------------------------------- */

template < class TNonlinearities >
nest::rate_neuron_ipn< TNonlinearities >::rate_neuron_ipn()
  : Archiving_Node()
  , P_()
  , S_()
  , B_( *this )
{
  recordablesMap_.create();
  Node::set_node_uses_wfr( kernel().simulation_manager.use_wfr() );
}

template < class TNonlinearities >
nest::rate_neuron_ipn< TNonlinearities >::rate_neuron_ipn(
  const rate_neuron_ipn& n )
  : Archiving_Node( n )
  , nonlinearities_( n.nonlinearities_ )
  , P_( n.P_ )
  , S_( n.S_ )
  , B_( n.B_, *this )
{
  Node::set_node_uses_wfr( kernel().simulation_manager.use_wfr() );
}

/* ----------------------------------------------------------------
 * Node initialization functions
 * ---------------------------------------------------------------- */

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::init_state_( const Node& proto )
{
  const rate_neuron_ipn& pr = downcast< rate_neuron_ipn >( proto );
  S_ = pr.S_;
}

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::init_buffers_()
{
  B_.delayed_rates_ex_.clear(); // includes resize
  B_.delayed_rates_in_.clear(); // includes resize

  // resize buffers
  const size_t buffer_size = kernel().connection_manager.get_min_delay();
  B_.instant_rates_ex_.resize( buffer_size, 0.0 );
  B_.instant_rates_in_.resize( buffer_size, 0.0 );
  B_.last_y_values.resize( buffer_size, 0.0 );
  B_.random_numbers.resize( buffer_size, numerics::nan );

  // initialize random numbers
  for ( unsigned int i = 0; i < buffer_size; i++ )
  {
    B_.random_numbers[ i ] =
      V_.normal_dev_( kernel().rng_manager.get_rng( get_thread() ) );
  }

  B_.logger_.reset(); // includes resize
  Archiving_Node::clear_history();
}

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::calibrate()
{
  B_.logger_
    .init(); // ensures initialization in case mm connected after Simulate

  const double h = Time::get_resolution().get_ms();

  if ( P_.lambda_ > 0 )
  {
    // use stochastic exponential Euler method
    V_.P1_ = std::exp( -P_.lambda_ * h / P_.tau_ );
    V_.P2_ = -1.0 / P_.lambda_ * numerics::expm1( -P_.lambda_ * h / P_.tau_ );
    V_.input_noise_factor_ = std::sqrt(
      -0.5 / P_.lambda_ * numerics::expm1( -2. * P_.lambda_ * h / P_.tau_ ) );
  }
  else
  {
    // use Euler-Maruyama method
    V_.P1_ = 1;
    V_.P2_ = h / P_.tau_;
    V_.input_noise_factor_ = std::sqrt( h / P_.tau_ );
  }
}

/* ----------------------------------------------------------------
 * Update and event handling functions
 */

template < class TNonlinearities >
bool
nest::rate_neuron_ipn< TNonlinearities >::update_( Time const& origin,
  const long from,
  const long to,
  const bool called_from_wfr_update )
{
  assert(
    to >= 0 && ( delay ) from < kernel().connection_manager.get_min_delay() );
  assert( from < to );

  const size_t buffer_size = kernel().connection_manager.get_min_delay();
  const double wfr_tol = kernel().simulation_manager.get_wfr_tol();
  bool wfr_tol_exceeded = false;

  // allocate memory to store rates to be sent by rate events
  std::vector< double > new_rates( buffer_size, 0.0 );

  for ( long lag = from; lag < to; ++lag )
  {
    // store rate
    new_rates[ lag ] = S_.rate_;
    // get noise
    S_.noise_ = P_.std_ * B_.random_numbers[ lag ];
    // propagate rate to new time step (exponential integration)
    S_.rate_ = V_.P1_ * new_rates[ lag ] + V_.P2_ * P_.mean_
      + V_.input_noise_factor_ * S_.noise_;

    double delayed_rates_in = 0;
    double delayed_rates_ex = 0;
    if ( called_from_wfr_update )
    {
      // use get_value_wfr_update to keep values in buffer
      delayed_rates_in = B_.delayed_rates_in_.get_value_wfr_update( lag );
      delayed_rates_ex = B_.delayed_rates_ex_.get_value_wfr_update( lag );
    }
    else
    {
      // use get_value to clear values in buffer after reading
      delayed_rates_in = B_.delayed_rates_in_.get_value( lag );
      delayed_rates_ex = B_.delayed_rates_ex_.get_value( lag );
    }
    double instant_rates_in = B_.instant_rates_in_[ lag ];
    double instant_rates_ex = B_.instant_rates_ex_[ lag ];
    double H_ex = 1.; // valid value for non-multiplicative coupling
    double H_in = 1.; // valid value for non-multiplicative coupling
    if ( P_.mult_coupling_ )
    {
      H_ex = nonlinearities_.mult_coupling_ex( new_rates[ lag ] );
      H_in = nonlinearities_.mult_coupling_in( new_rates[ lag ] );
    }

    if ( P_.linear_summation_ )
    {
      // In this case we explicitly need to distinguish the cases of
      // multiplicative coupling and non-multiplicative coupling in
      // order to compute input( ex + in ) instead of input(ex) + input(in) in
      // the non-multiplicative case.
      if ( P_.mult_coupling_ )
      {
        S_.rate_ += V_.P2_ * H_ex
          * nonlinearities_.input( delayed_rates_ex + instant_rates_ex );
        S_.rate_ += V_.P2_ * H_in
          * nonlinearities_.input( delayed_rates_in + instant_rates_in );
      }
      else
      {
        S_.rate_ += V_.P2_
          * nonlinearities_.input( delayed_rates_ex + instant_rates_ex
              + delayed_rates_in + instant_rates_in );
      }
    }
    else
    {
      // In this case multiplicative and non-multiplicative coupling
      // can be handled with the same code.
      S_.rate_ += V_.P2_ * H_ex * ( delayed_rates_ex + instant_rates_ex );
      S_.rate_ += V_.P2_ * H_in * ( delayed_rates_in + instant_rates_in );
    }

    if ( P_.rectify_output_ and S_.rate_ < 0 )
    {
      S_.rate_ = 0;
    }

    if ( called_from_wfr_update )
    {
      // check if deviation from last iteration exceeds wfr_tol
      wfr_tol_exceeded = wfr_tol_exceeded
        or fabs( S_.rate_ - B_.last_y_values[ lag ] ) > wfr_tol;
      // update last_y_values for next wfr iteration
      B_.last_y_values[ lag ] = S_.rate_;
    }
    else
    {
      // rate logging
      B_.logger_.record_data( origin.get_steps() + lag );
    }
  }

  if ( not called_from_wfr_update )
  {
    // Send delay-rate-neuron-event. This only happens in the final iteration
    // to avoid accumulation in the buffers of the receiving neurons.
    DelayedRateConnectionEvent drve;
    drve.set_coeffarray( new_rates );
    kernel().event_delivery_manager.send_secondary( *this, drve );

    // clear last_y_values
    std::vector< double >( buffer_size, 0.0 ).swap( B_.last_y_values );

    // modifiy new_rates for rate-neuron-event as proxy for next min_delay
    for ( long temp = from; temp < to; ++temp )
      new_rates[ temp ] = S_.rate_;

    // create new random numbers
    B_.random_numbers.resize( buffer_size, numerics::nan );
    for ( unsigned int i = 0; i < buffer_size; i++ )
    {
      B_.random_numbers[ i ] =
        V_.normal_dev_( kernel().rng_manager.get_rng( get_thread() ) );
    }
  }

  // Send rate-neuron-event
  InstantaneousRateConnectionEvent rve;
  rve.set_coeffarray( new_rates );
  kernel().event_delivery_manager.send_secondary( *this, rve );

  // Reset variables
  std::vector< double >( buffer_size, 0.0 ).swap( B_.instant_rates_ex_ );
  std::vector< double >( buffer_size, 0.0 ).swap( B_.instant_rates_in_ );

  return wfr_tol_exceeded;
}


template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::handle(
  InstantaneousRateConnectionEvent& e )
{
  size_t i = 0;
  std::vector< unsigned int >::iterator it = e.begin();
  // The call to get_coeffvalue( it ) in this loop also advances the iterator it
  while ( it != e.end() )
  {
    if ( P_.linear_summation_ )
    {
      if ( e.get_weight() >= 0.0 )
      {
        B_.instant_rates_ex_[ i ] += e.get_weight() * e.get_coeffvalue( it );
      }
      else
      {
        B_.instant_rates_in_[ i ] += e.get_weight() * e.get_coeffvalue( it );
      }
    }
    else
    {
      if ( e.get_weight() >= 0.0 )
      {
        B_.instant_rates_ex_[ i ] +=
          e.get_weight() * nonlinearities_.input( e.get_coeffvalue( it ) );
      }
      else
      {
        B_.instant_rates_in_[ i ] +=
          e.get_weight() * nonlinearities_.input( e.get_coeffvalue( it ) );
      }
    }
    i++;
  }
}

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::handle(
  DelayedRateConnectionEvent& e )
{
  size_t i = 0;
  std::vector< unsigned int >::iterator it = e.begin();
  // The call to get_coeffvalue( it ) in this loop also advances the iterator it
  while ( it != e.end() )
  {
    if ( P_.linear_summation_ )
    {
      if ( e.get_weight() >= 0.0 )
      {
        B_.delayed_rates_ex_.add_value(
          e.get_delay() - kernel().connection_manager.get_min_delay() + i,
          e.get_weight() * e.get_coeffvalue( it ) );
      }
      else
      {
        B_.delayed_rates_in_.add_value(
          e.get_delay() - kernel().connection_manager.get_min_delay() + i,
          e.get_weight() * e.get_coeffvalue( it ) );
      }
    }
    else
    {
      if ( e.get_weight() >= 0.0 )
      {
        B_.delayed_rates_ex_.add_value(
          e.get_delay() - kernel().connection_manager.get_min_delay() + i,
          e.get_weight() * nonlinearities_.input( e.get_coeffvalue( it ) ) );
      }
      else
      {
        B_.delayed_rates_in_.add_value(
          e.get_delay() - kernel().connection_manager.get_min_delay() + i,
          e.get_weight() * nonlinearities_.input( e.get_coeffvalue( it ) ) );
      }
    }
    ++i;
  }
}

template < class TNonlinearities >
void
nest::rate_neuron_ipn< TNonlinearities >::handle( DataLoggingRequest& e )
{
  B_.logger_.handle( e );
}

} // namespace

#endif /* #ifndef RATE_NEURON_IPN_IMPL_H */
