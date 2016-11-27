extern crate clap;
#[macro_use] extern crate log;
extern crate env_logger;
use clap::{Arg, App, ArgMatches, AppSettings};
mod seed;
mod mutate;
mod fuzz;

fn arg_parse<'a> () -> ArgMatches<'a> {
  App::new("baeum")
    .setting(AppSettings::TrailingVarArg)
    .setting(AppSettings::AllowLeadingHyphen)
    .version("0.0.1")
    .author("Choongwoo Han <cwhan.tunz@gmail.com>")
    .about("A Reinforcement-Learning-Based Fuzzing")
    .arg(Arg::with_name("seeds")
       .short("s")
       .help("Directory of seed files")
       .required(true)
       .takes_value(true))
    .arg(Arg::with_name("output")
       .short("o")
       .help("Directory of output files")
       .required(true)
       .takes_value(true))
    .arg(Arg::from_usage("<args>... 'commands to run'"))
    .get_matches()
}

fn main() {
  env_logger::init().unwrap();

  let matches = arg_parse();
  let seeds_dir = matches.value_of("seeds").unwrap();
  let output_dir = matches.value_of("output").unwrap();
  let args: Vec<&str> = matches.values_of("args").unwrap().collect();

  debug!("Seed Dir: {}", seeds_dir);
  debug!("Output Dir: {}", output_dir);
  debug!("Command Line: {:?}", args);

  let seeds = match seed::load_seed_files(seeds_dir) {
                Ok(v) => v,
                Err(e) => { println!("Error: {}", e); return}
              };

  fuzz::fuzz(seeds, args, output_dir);
}
