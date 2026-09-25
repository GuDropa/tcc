#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include "globals.h"


#define SPREAD_MODULE
#define SWGHT_TYPE float
#define SLOPE_WEIGHT_ARRAY_SZ 256


void     spr_spiral (int index,                                   
                int *i_out,                                  
                int *j_out);                               

void     spr_phase1n3 (COEFF_TYPE diffusion_coefficient,          
                  COEFF_TYPE breed_coefficient,              
                  GRID_P z,                                  
                  GRID_P delta,                              
                  GRID_P slp,                                
                  GRID_P excld,                              
                  SWGHT_TYPE * swght,                        
                  int *sng,                                  
                  int *sdc);                               

void     spr_phase4 (COEFF_TYPE spread_coefficient,               
                GRID_P z,                                    
                GRID_P excld,                                
                GRID_P delta,                                
                GRID_P slp,                                  
                SWGHT_TYPE * swght,                          
                int *og);                                  


void     spr_phase5 (COEFF_TYPE road_gravity,                     
                COEFF_TYPE diffusion_coefficient,            
                COEFF_TYPE breed_coefficient,                
                GRID_P z,                                    
                GRID_P delta,                                
                GRID_P slp,                                  
                GRID_P excld,                                
                GRID_P roads,                                
                SWGHT_TYPE * swght,                          
                int *rt,                                     
                GRID_P workspace);                         /* MOD    */

void     spr_get_slp_weights (int array_size,                     
                         SWGHT_TYPE * lut);                

bool spr_road_search (int i_grwth_center,          
                                int j_grwth_center,          
                                int *i_road,                 
                                int *j_road,                 
                                int max_search_index,        
                                GRID_P roads);             

bool spr_road_walk (int i_road_start,                   
                         int j_road_start,                   
                         int *i_road_end,                    
                         int *j_road_end,                    
                         GRID_P roads,                       
                         double diffusion_coefficient);    

bool spr_urbanize_nghbr (int i,                         
                              int j,                         
                              int *i_nghbr,                  
                              int *j_nghbr,                  
                              GRID_P z,                      
                              GRID_P delta,                  
                              GRID_P slp,                    
                              GRID_P excld,                  
                              SWGHT_TYPE * swght,            
                              PIXEL pixel_value,             
                              int *stat);                  

void spr_get_neighbor (int i_in,                           
                         int j_in,                           
                         int *i_out,                         
                         int *j_out);                      

bool    spr_urbanize (int row,                                   
                  int col,                                   
                  GRID_P z,                                  
                  GRID_P delta,                              
                  GRID_P slp,                                
                  GRID_P excld,                              
                  SWGHT_TYPE * swght,                        
                  PIXEL pixel_value,                         
                  int *stat);                              

COEFF_TYPE     spr_GetDiffusionValue (COEFF_TYPE diffusion_coeff);    /* IN    */
COEFF_TYPE     spr_GetRoadGravValue (COEFF_TYPE rg_coeff);            /* IN    */



void   spr_phase1n3 (COEFF_TYPE diffusion_coefficient,            
                COEFF_TYPE breed_coefficient,                
                GRID_P z,                                    
                GRID_P delta,                                
                GRID_P slp,                                  
                GRID_P excld,                                
                SWGHT_TYPE * swght,                          
                int *sng,                                    
                int *sdc)                                  
{
  char func[] = "spr_phase1n3";
  int i;
  int j;
  int i_out;
  int j_out;
  int k;
  int count;
  int tries;
  int max_tries;
  COEFF_TYPE diffusion_value;
  bool urbanized;


  diffusion_value = spr_GetDiffusionValue (diffusion_coefficient);

  for (k = 0; k < 1 + (int) diffusion_value; k++)
  {
    i = RANDOM_ROW;
    j = RANDOM_COL;

    if (INTERIOR_PT (i, j))
    {
      if (spr_urbanize (i,                                     
                        j,                                     
                        z,                                     
                        delta,                                 
                        slp,                                   
                        excld,                                 
                        swght,                                 
                        PHASE1G,                               
                        sng))                              
      {
        if (RANDOM_INT (101) < (int) breed_coefficient)
        {
          count = 0;
          max_tries = 8;
          for (tries = 0; tries < max_tries; tries++)
          {
            urbanized = FALSE;
            urbanized =
              spr_urbanize_nghbr (i,                         
                                  j,                         
                                  &i_out,                    
                                  &j_out,                    
                                  z,                         
                                  delta,                     
                                  slp,                       
                                  excld,                     
                                  swght,                     
                                  PHASE3G,                   
                                  sdc);                    
            if (urbanized)
            {
              count++;
              if (count == MIN_NGHBR_TO_SPREAD)
              {
                break;
              }
            }
          }
        }
      }
    }
  }

}



void   spr_phase4 (COEFF_TYPE spread_coefficient,                 
              GRID_P z,                                      
              GRID_P excld,                                  
              GRID_P delta,                                  
              GRID_P slp,                                    
              SWGHT_TYPE * swght,                            
              int *og)                                     
{

  int row;
  int col;
  int row_nghbr;
  int col_nghbr;
  int pixel;
  int walkabout_row[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
  int walkabout_col[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
  int urb_count;
  int nrows;
  int ncols;

  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();
  assert (nrows > 0);
  assert (ncols > 0);

  for (row = 1; row < nrows - 1; row++)
  {
    for (col = 1; col < ncols - 1; col++)
    {

      if ((z[OFFSET (row, col)] > 0) &&
          (RANDOM_INT (101) < spread_coefficient))
      {

        urb_count = util_count_neighbors (z, row, col, GT, 0);
        if ((urb_count >= 2) && (urb_count < 8))
        {
          pixel = RANDOM_INT (8);

          row_nghbr = row + walkabout_row[pixel];
          col_nghbr = col + walkabout_col[pixel];

          spr_urbanize (row_nghbr,                           
                        col_nghbr,                           
                        z,                                   
                        delta,                               
                        slp,                                 
                        excld,                               
                        swght,                               
                        PHASE4G,                             
                        og);                               
        }
      }
    }
  }

}

void   spr_phase5 (COEFF_TYPE road_gravity,                       
              COEFF_TYPE diffusion_coefficient,              
              COEFF_TYPE breed_coefficient,                  
              GRID_P z,                                      
              GRID_P delta,                                  
              GRID_P slp,                                    
              GRID_P excld,                                  
              GRID_P roads,                                  
              SWGHT_TYPE * swght,                            
              int *rt,                                       
              GRID_P workspace)                            /* MOD    */

{

  int iii;
  int int_road_gravity;
  int growth_count;
  int *growth_row;
  int *growth_col;
  int max_search_index;
  int growth_index;
  bool road_found;
  int i_rd_start;
  int j_rd_start;
  int max_tries;
  bool spread;
  bool urbanized;
  int i_rd_end;
  int j_rd_end;
  int i_rd_end_nghbr;
  int j_rd_end_nghbr;
  int i_rd_end_nghbr_nghbr;
  int j_rd_end_nghbr_nghbr;
  int tries;
  int nrows;
  int ncols;
  int total_pixels;


  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();



  total_pixels = mem_GetTotalPixels ();

  growth_row = (int *) workspace;
  growth_col = (int *) workspace + (nrows);

  growth_count = 0;

  for (iii = 0; iii < total_pixels; iii++)
  {
    if (delta[iii] > 0)
    {
      growth_row[growth_count] = iii / ncols;
      growth_col[growth_count] = iii % ncols;
      growth_count++;
    }
  }

  if (growth_count > 0)
  {
    for (iii = 0; iii < 1 + (int) (breed_coefficient); iii++)
    {

      int_road_gravity = spr_GetRoadGravValue (road_gravity);
      max_search_index = 4 * (int_road_gravity * (1 + int_road_gravity));
      max_search_index = MAX (max_search_index, nrows);
      max_search_index = MAX (max_search_index, ncols);


      growth_index = (int) ((double) growth_count * RANDOM_FLOAT);


      road_found =
        spr_road_search (growth_row[growth_index],
                         growth_col[growth_index],
                         &i_rd_start,
                         &j_rd_start,
                         max_search_index,
                         roads);


      if (road_found)
      {
        spread = spr_road_walk (i_rd_start,                  
                                j_rd_start,                  
                                &i_rd_end,                   
                                &j_rd_end,                   
                                roads,                       
                                diffusion_coefficient);    

        if (spread == TRUE)
        {
          urbanized =
            spr_urbanize_nghbr (i_rd_end,                    
                                j_rd_end,                    
                                &i_rd_end_nghbr,             
                                &j_rd_end_nghbr,             
                                z,                           
                                delta,                       
                                slp,                         
                                excld,                       
                                swght,                       
                                PHASE5G,                     
                                rt);                       
          if (urbanized)
          {
            max_tries = 3;
            for (tries = 0; tries < max_tries; tries++)
            {
              urbanized =
                spr_urbanize_nghbr (i_rd_end_nghbr,          
                                    j_rd_end_nghbr,          
                                    &i_rd_end_nghbr_nghbr,   
                                    &j_rd_end_nghbr_nghbr,   
                                    z,                       
                                    delta,                   
                                    slp,                     
                                    excld,                   
                                    swght,                   
                                    PHASE5G,                 
                                    rt);                   

            }
          }
        }
      }
    }
  }

}


void   spr_get_slp_weights (int array_size,                       
                       SWGHT_TYPE * lut)                   
{

  float val;
  float exp;
  int i;

  exp = coeff_GetCurrentSlopeResist () / (MAX_SLOPE_RESISTANCE_VALUE / 2.0);
  for (i = 0; i < array_size; i++)
  {
    if (i < scen_GetCriticalSlope ())
    {
      val = (scen_GetCriticalSlope () - (SWGHT_TYPE) i) / scen_GetCriticalSlope ();
      lut[i] = 1.0 - pow (val, exp);
    }
    else
    {
      lut[i] = 1.0;
    }
  }
}




COEFF_TYPE
  spr_GetDiffusionValue (COEFF_TYPE diffusion_coeff)
{

  COEFF_TYPE diffusion_value;
  double rows_sq;
  double cols_sq;

  rows_sq = igrid_GetNumRows () * igrid_GetNumRows ();
  cols_sq = igrid_GetNumCols () * igrid_GetNumCols ();

  /*
   * diffusion_value's MAXIMUM (IF diffusion_coeff == 100)
   * WILL BE 5% OF THE IMAGE DIAGONAL. 
   */

  diffusion_value = ((diffusion_coeff * 0.005) * sqrt (rows_sq + cols_sq));
  return diffusion_value;
}


COEFF_TYPE  spr_GetRoadGravValue (COEFF_TYPE rg_coeff)
{

  int rg_value;
  int row;
  int col;

  row = igrid_GetNumRows ();
  col = igrid_GetNumCols ();

  /*
   * rg_value's MAXIMUM (IF rg_coeff == 100)
   * WILL BE 1/16 OF THE IMAGE DIMENSIONS. 
   */

  rg_value = (rg_coeff / MAX_ROAD_VALUE) * ((row + col) / 16.0);

  return rg_value;
}


bool
  spr_urbanize (int row,                                     
                int col,                                     
                GRID_P z,                                    
                GRID_P delta,                                
                GRID_P slp,                                  
                GRID_P excld,                                
                SWGHT_TYPE * swght,                          
                PIXEL pixel_value,                           
                int *stat)                                 
{
  bool val;
  int nrows;
  int ncols;

  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();


  val = FALSE;
  if (z[OFFSET ((row), (col))] == 0)
  {
    if (delta[OFFSET ((row), (col))] == 0)
    {
      if (RANDOM_FLOAT > swght[slp[OFFSET ((row), (col))]])
      {
        if (excld[OFFSET ((row), (col))] < RANDOM_INT (100))
        {
          val = TRUE;
          delta[OFFSET (row, col)] = pixel_value;
          (*stat)++;
          stats_IncrementUrbanSuccess ();
        }
        else
        {
          stats_IncrementEcludedFailure ();
        }
      }
      else
      {
        stats_IncrementSlopeFailure ();
      }
    }
    else
    {
      stats_IncrementDeltaFailure ();
    }
  }
  else
  {
    stats_IncrementZFailure ();
  }


  return val;
}

static
  void   spr_get_neighbor (int i_in,                                
                    int j_in,                                
                    int *i_out,                              
                    int *j_out)                            
{
  int i;
  int j;
  int k;
  int nrows;
  int ncols;

  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();

  util_get_next_neighbor (i_in, j_in, i_out, j_out, RANDOM_INT (8));
  for (k = 0; k < 8; k++)
  {
    i = (*i_out);
    j = (*j_out);
    if (IMAGE_PT (i, j))
    {
      break;
    }
    util_get_next_neighbor (i_in, j_in, i_out, j_out, -1);
  }
}

static
    bool
  spr_urbanize_nghbr (int i,                                 
                      int j,                                 
                      int *i_nghbr,                          
                      int *j_nghbr,                          
                      GRID_P z,                              
                      GRID_P delta,                          
                      GRID_P slp,                            
                      GRID_P excld,                          
                      SWGHT_TYPE * swght,                    
                      PIXEL pixel_value,                     
                      int *stat)                           
{

  bool status = FALSE;

  if (IMAGE_PT (i, j))
  {
    spr_get_neighbor (i,                                     /* IN    */
                      j,                                     /* IN    */
                      i_nghbr,                               /* OUT   */
                      j_nghbr);                            /* OUT   */

    status = spr_urbanize ((*i_nghbr),                       
                           (*j_nghbr),                       
                           z,                                
                           delta,                            
                           slp,                              
                           excld,                            
                           swght,                            
                           pixel_value,                      
                           stat);                          
  }
  return status;
}


static
    bool
  spr_road_walk (int i_road_start,                           
                 int j_road_start,                           
                 int *i_road_end,                            
                 int *j_road_end,                            
                 GRID_P roads,                               
                 double diffusion_coefficient)             
{
  int i;
  int j;
  int i_nghbr;
  int j_nghbr;
  int k;
  bool end_of_road;
  bool spread = FALSE;
  int run_value;
  int run = 0;

  i = i_road_start;
  j = j_road_start;
  end_of_road = FALSE;
  while (!end_of_road)
  {
    end_of_road = TRUE;
    util_get_next_neighbor (i, j, &i_nghbr, &j_nghbr, RANDOM_INT (8));
    for (k = 0; k < 8; k++)
    {
      if (IMAGE_PT (i_nghbr, j_nghbr))
      {
        if (roads[OFFSET (i_nghbr, j_nghbr)])
        {
          end_of_road = FALSE;
          run++;
          i = i_nghbr;
          j = j_nghbr;
          break;
        }
      }
      util_get_next_neighbor (i, j, &i_nghbr, &j_nghbr, -1);
    }
    run_value = (int) (roads[OFFSET (i, j)] / MAX_ROAD_VALUE *
                       diffusion_coefficient);
    if (run > run_value)
    {
      end_of_road = TRUE;
      spread = TRUE;
      (*i_road_end) = i;
      (*j_road_end) = j;
    }
  }

  return spread;
}



static
    bool
  spr_road_search (int i_grwth_center,                       
                   int j_grwth_center,                       
                   int *i_road,                              
                   int *j_road,                              
                   int max_search_index,                     
                   GRID_P roads)                           
{
  int i;
  int j;
  int i_offset;
  int j_offset;
  bool road_found = FALSE;
  int srch_index;

  for (srch_index = 0; srch_index < max_search_index; srch_index++)
  {
    spr_spiral (srch_index, &i_offset, &j_offset);
    i = i_grwth_center + i_offset;
    j = j_grwth_center + j_offset;

    if (IMAGE_PT (i, j))
    {
      if (roads[OFFSET (i, j)])
      {
        road_found = TRUE;
        (*i_road) = i;
        (*j_road) = j;
        break;
      }
    }
  }


  return road_found;
}



void   spr_spiral (int index,                                     
              int *i_out,                                    
              int *j_out)                                  
{
  bool bn_found;
  int i;
  int j;
  int bn;
  int bo;
  int total;
  int left_side_len;
  int right_side_len;
  int top_len;
  int bot_len;
  int range1;
  int range2;
  int range3;
  int range4;
  int region_offset;
  int nrows;
  int ncols;

  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();

  bn_found = FALSE;
  for (bn = 1; bn < MAX (ncols, nrows); bn++)
  {
    total = 8 * ((1 + bn) * bn) / 2;
    if (total > index)
    {
      bn_found = TRUE;
      break;
    }
  }
  if (!bn_found)
  {
    exit (1);
  }
  bo = index - 8 * ((bn - 1) * bn) / 2;
  left_side_len = right_side_len = bn * 2 + 1;
  top_len = bot_len = bn * 2 - 1;
  range1 = left_side_len;
  range2 = left_side_len + bot_len;
  range3 = left_side_len + bot_len + right_side_len;
  range4 = left_side_len + bot_len + right_side_len + top_len;
  if (bo < range1)
  {
    region_offset = bo % range1;
    i = -bn + region_offset;
    j = -bn;
  }
  else if (bo < range2)
  {
    region_offset = (bo - range1) % range2;
    i = bn;
    j = -bn + 1 + region_offset;
  }
  else if (bo < range3)
  {
    region_offset = (bo - range2) % range3;
    i = bn - region_offset;
    j = bn;
  }
  else if (bo < range4)
  {
    region_offset = (bo - range3) % range4;
    i = -bn;
    j = bn - 1 - region_offset;
  }
  else
  {
    exit (1);
  }
  *i_out = i;
  *j_out = j;
}


void   spr_spread (
               float *average_slope,                         
               int *num_growth_pix,                          
               int *sng,
               int *sdc,
               int *og,
               int *rt,
               int *pop,
               GRID_P z                                      
  )                                                        /* MOD    */
{

  GRID_P delta;
  int i;
  int total_pixels;
  int nrows;
  int ncols;
  double road_gravity;
  COEFF_TYPE diffusion_coefficient;
  COEFF_TYPE breed_coefficient;
  COEFF_TYPE spread_coefficient;
  GRID_P excld;
  GRID_P roads;
  GRID_P slp;
  GRID_P scratch_gif1;
  GRID_P scratch_gif3;
  SWGHT_TYPE swght[SLOPE_WEIGHT_ARRAY_SZ];

  road_gravity = coeff_GetCurrentRoadGravity ();
  diffusion_coefficient = coeff_GetCurrentDiffusion ();
  breed_coefficient = coeff_GetCurrentBreed ();
  spread_coefficient = coeff_GetCurrentSpread ();

  scratch_gif1 = mem_GetWGridPtr (__FILE__, func, __LINE__);
  scratch_gif3 = mem_GetWGridPtr (__FILE__, func, __LINE__);

  excld = igrid_GetExcludedGridPtr (__FILE__, func, __LINE__);
  roads = igrid_GetRoadGridPtrByYear (__FILE__, func,
                                      __LINE__, proc_GetCurrentYear ());
  slp = igrid_GetSlopeGridPtr (__FILE__, func, __LINE__);


  total_pixels = mem_GetTotalPixels ();
  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();




  delta = scratch_gif1;


  util_init_grid (delta, 0);


  spr_get_slp_weights (SLOPE_WEIGHT_ARRAY_SZ,                
                       swght);                             


  spr_phase1n3 (diffusion_coefficient,                       
                breed_coefficient,                           
                z,                                           
                delta,                                       
                slp,                                         
                excld,                                       
                swght,                                       
                sng,                                         
                sdc);                                      


  spr_phase4 (spread_coefficient,                            
              z,                                             
              excld,                                         
              delta,                                         
              slp,                                           
              swght,                                         
              og);                                         

  spr_phase5 (road_gravity,                                  
              diffusion_coefficient,                         
              breed_coefficient,                             
              z,                                             
              delta,                                         
              slp,                                           
              excld,                                         
              roads,                                         
              swght,                                         
              rt,                                            
              scratch_gif3);                               /* MOD    */


  util_condition_gif (total_pixels,                          
                      delta,                                 
                      GT,                                    
                      PHASE5G,                               
                      delta,                                 
                      0);                                  

  util_condition_gif (total_pixels,                          
                      excld,                                 
                      GE,                                    
                      100,                                   
                      delta,                                 
                      0);                                  


  (*num_growth_pix) = 0;
  (*average_slope) = 0.0;

  for (i = 0; i < total_pixels; i++)
  {
    if ((z[i] == 0) && (delta[i] > 0))
    {
      /* new growth being placed into array */
      (*average_slope) += (float) slp[i];
      z[i] = delta[i];
      (*num_growth_pix)++;
    }
  }
  *pop = util_count_pixels (total_pixels, z, GE, PHASE0G);

  if (*num_growth_pix == 0)
  {
    *average_slope = 0.0;
  }
  else
  {
    *average_slope /= (float) *num_growth_pix;
  }

  roads = igrid_GridRelease (__FILE__, func, __LINE__, roads);
  excld = igrid_GridRelease (__FILE__, func, __LINE__, excld);
  slp = igrid_GridRelease (__FILE__, func, __LINE__, slp);
  scratch_gif1 = mem_GetWGridFree (__FILE__, func, __LINE__, scratch_gif1);
  scratch_gif3 = mem_GetWGridFree (__FILE__, func, __LINE__, scratch_gif3);

}
