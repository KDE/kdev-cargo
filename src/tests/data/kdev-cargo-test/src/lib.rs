#[cfg(test)]
mod tests {
    #[test]
    fn passes() {
        assert_eq!(2 + 2, 4);
    }

    #[test]
    fn fails() {
        assert_eq!(2 + 2, 5);
    }

    #[test]
    #[should_panic]
    fn should_fail_and_fails() {
        assert_eq!(2 + 2, 5);
    }

    #[test]
    #[should_panic]
    fn should_fail_and_passes() {
        assert_eq!(2 + 2, 4);
    }

    #[test]
    #[ignore]
    fn is_ignored_and_passes() {
        assert_eq!(2 + 2, 4);
    }

    #[test]
    #[ignore]
    fn is_ignored_and_fails() {
        assert_eq!(2 + 2, 5);
    }
}
