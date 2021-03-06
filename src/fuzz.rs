use seed::Seed;
use conf::Conf;
use utils::get_random;
use mutate;
use exec;

pub fn dry_run(conf: &Conf, seeds: &Vec<Seed>) {
    let mut total_node = 0;
    for seed in seeds {
        let content = seed.load_buf();
        let (_, feedback) = exec::run_target(&conf, &content);
        total_node += if total_node == 0 {
            feedback.node
        } else {
            feedback.newnode
        };
    }
    let mut log = conf.log.write().unwrap();
    log.info.exec_count = seeds.len() as u64;
    log.info.total_node = total_node;
}

fn fuzz_one(conf: &Conf, seed: &Seed, q: &Vec<Seed>) -> Vec<Seed> {
    let mut new_seeds = vec![];
    let content = seed.load_buf();

    for _ in 0..10 {
        let content = mutate::mutate(&content, q);
        let (status, feedback) = exec::run_target(&conf, &content);
        conf.update_exec_info(feedback.newnode);
        if !exec::is_crash(status) && feedback.newnode > 0 {
            let new_seed = Seed::new(conf, &content);
            new_seeds.push(new_seed);
            break;
        }
    }

    new_seeds
}

pub fn fuzz(conf: Conf, seeds: Vec<Seed>) {
    let mut q = seeds;

    loop {
        let new_seeds = {
            let seed = &q[get_random(q.len())];
            fuzz_one(&conf, seed, &q)
        };
        q.extend(new_seeds);
        conf.update_log();
    }
}
