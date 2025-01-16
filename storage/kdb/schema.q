/ kdb+ tick store schema for lobsim tapes (kdb+ 4.0 32-bit personal edition).
/ One table per event kind; price in ticks (long) so joins are exact, px/size in float for reporting.
levels:([] time:`timestamp$(); sym:`symbol$(); side:`char$(); price:`long$(); qty:`long$())
trades:([] time:`timestamp$(); sym:`symbol$(); side:`char$(); price:`long$(); qty:`long$())
quotes:([] time:`timestamp$(); sym:`symbol$(); bid:`long$(); ask:`long$(); bidq:`long$(); askq:`long$())
