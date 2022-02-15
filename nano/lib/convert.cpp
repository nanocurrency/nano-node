#include <nano/lib/convert.hpp>

std::string convert_raw_to_dec (std::string amount_raw, nano::uint128_t ratio)
{
	std::string amount_in_dec = amount_raw; //initialize variable
	int amount_length = amount_raw.length (); //count digits of input
	int divider_length = ratio.convert_to<std::string> ().length (); //count digits of ratio divider

	if (divider_length > amount_length) // if amount is less than 1 whole unit of the desired output
	{
		int rest = divider_length - amount_length; // calculate the number of zeros we need after the decimal separator
		for (int i = 1; i < rest; i++)
		{
			amount_in_dec = "0" + amount_in_dec; // add another trailing zero after decimal separator
		}
		amount_in_dec = "0." + amount_in_dec; // add the leading zero
	}
	else // if amount is at least one whole unit of the desired output
	{
		amount_in_dec.insert (amount_in_dec.end () - (divider_length - 1), '.'); // add a dot according to the desired divider
	}

	int unnecessarytrailingzeroscounter = 0;
	while (amount_in_dec[amount_in_dec.length () - 1 - unnecessarytrailingzeroscounter] == '0')
	{
		unnecessarytrailingzeroscounter++; // count trailing zeros
	}
	// if there's no trailing decimals at all because we solely have a whole unit, then lower the counter to keep one 0 after the dot
	if (unnecessarytrailingzeroscounter == (divider_length - 1))
	{
		unnecessarytrailingzeroscounter--;
	}

	amount_in_dec.erase (amount_in_dec.length () - unnecessarytrailingzeroscounter, amount_in_dec.length ()); // remove unnecessary trailing zeros. first parameter is the pos after which the cutoff begins, second parameter is supposed to be the number of characters to be truncated but if it's chosen too high it will just stop at the end
	return amount_in_dec;
}